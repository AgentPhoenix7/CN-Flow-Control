#include "selective_repeat.hpp"

#include <chrono>
#include <fstream>
#include <map>
#include <stdexcept>

#include "frame.hpp"
#include "timer.hpp"
#include "wire.hpp"

namespace basic_flow {

namespace {

using Clock = std::chrono::steady_clock;

double ms_between(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

// Forward distance from 'from' to 'to' around the modulo-256 sequence space.
int forward_distance(std::uint8_t from, std::uint8_t to) {
  return (static_cast<int>(to) - static_cast<int>(from) + 256) % 256;
}

}  // namespace

RunStats run_selective_repeat_sender(const TcpSocket& socket, const std::vector<std::uint8_t>& file_bytes, int window, ChannelConfig data_channel_cfg, unsigned seed) {
  if (window < 1 || window > 128) throw std::invalid_argument("selective-repeat window must be 1-128");

  RunStats stats;
  Channel channel(data_channel_cfg, seed);
  RttTimer timer;
  auto frames = split_into_frames(file_bytes);
  const std::size_t total = frames.size();
  std::vector<Clock::time_point> sent_at(total);
  std::vector<bool> acked(total, false);

  auto send_frame = [&](std::size_t index, bool is_retransmit) {
    auto body = serialize(frames[index]);
    bool delivered = channel.apply(body);
    sent_at[index] = Clock::now();
    if (delivered) send_message(socket, TAG_DATA, body);
    stats.frames_sent++;
    if (is_retransmit) stats.retransmissions++;
  };

  auto start = Clock::now();
  std::size_t base = 0;
  std::size_t next = 0;

  while (base < total) {
    while (next < total && next - base < static_cast<std::size_t>(window)) {
      send_frame(next, false);
      next++;
    }

    auto msg = try_recv_message(socket, static_cast<int>(timer.timeout_ms()));
    if (msg.status == RecvStatus::Closed) {
      throw std::runtime_error("connection closed while waiting for ACK");
    }
    if (msg.status == RecvStatus::Timeout) {
      stats.timeouts++;
    } else if (msg.tag == TAG_ACK) {
      if (auto ack_seq = parse_ack_body(msg.body)) {
        for (std::size_t i = base; i < next; ++i) {
          if (!acked[i] && frames[i].seq == *ack_seq) {
            acked[i] = true;
            timer.on_sample(ms_between(sent_at[i], Clock::now()));
            stats.acks_received++;
            break;
          }
        }
      }
    }
    while (base < total && acked[base]) base++;

    // Independent per-frame timers: whatever triggered this loop iteration, resend
    // any still-outstanding frame whose own wait has exceeded the current timeout.
    auto now = Clock::now();
    for (std::size_t i = base; i < next; ++i) {
      if (!acked[i] && ms_between(sent_at[i], now) >= timer.timeout_ms()) {
        send_frame(i, true);
      }
    }
  }

  send_message(socket, TAG_DONE);
  // A spurious per-frame timeout near the end can leave stray duplicate ACKs ahead
  // of the receiver's DONE_ACK in the stream; drain anything that isn't DONE_ACK
  // instead of failing on it. DONE_ACK itself is a control message, not subject to
  // channel impairment, so it is guaranteed to eventually arrive.
  RecvMessage done_ack;
  do {
    done_ack = try_recv_message(socket, 0);
    if (done_ack.status == RecvStatus::Closed) {
      throw std::runtime_error("connection closed while waiting for DONE_ACK");
    }
  } while (done_ack.tag != TAG_DONE_ACK);
  stats.elapsed_ms = ms_between(start, Clock::now());
  return stats;
}

void run_selective_repeat_receiver(const TcpSocket& socket, const std::string& output_path, int window, ChannelConfig ack_channel_cfg, unsigned seed) {
  if (window < 1 || window > 128) throw std::invalid_argument("selective-repeat window must be 1-128");

  Channel ack_channel(ack_channel_cfg, seed);
  std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot open output file: " + output_path);

  std::uint8_t next_deliver = 0;
  std::map<std::uint8_t, std::vector<std::uint8_t>> buffer;  // seq -> true-length payload

  auto send_ack = [&](std::uint8_t seq) {
    auto body = make_ack_body(seq);
    if (ack_channel.apply(body)) send_message(socket, TAG_ACK, body);
  };

  while (true) {
    auto msg = try_recv_message(socket, 0);
    if (msg.status == RecvStatus::Closed) throw std::runtime_error("sender closed connection early");
    if (msg.tag == TAG_DONE) {
      send_message(socket, TAG_DONE_ACK);
      break;
    }
    if (msg.tag != TAG_DATA) continue;

    DataFrame frame;
    if (!deserialize_and_verify(msg.body, frame)) continue;  // corrupted: discard, no ACK

    if (forward_distance(next_deliver, frame.seq) < window) {
      // Within the receive window: buffer it (re-buffering a duplicate is harmless)
      // and ACK it independently of delivery order.
      buffer[frame.seq] =
          std::vector<std::uint8_t>(frame.payload.begin(), frame.payload.begin() + frame.length);
      send_ack(frame.seq);
      while (buffer.count(next_deliver) != 0) {
        const auto& payload = buffer[next_deliver];
        out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        buffer.erase(next_deliver);
        next_deliver = static_cast<std::uint8_t>(next_deliver + 1);
      }
    } else {
      // Already delivered and outside the current window: re-ACK so a lost
      // original ACK does not stall the sender.
      send_ack(frame.seq);
    }
  }
}

}  // namespace basic_flow

/*
FORWARD DISTANCE:

distance = (to - from + 256) mod 256

return distance
*/

/*
SENDER:

check that window is between 1 and 128

create channel
create RTT timer
split file into frames

create sending-time array
create ACK-status array initialized to false

base = 0
next = 0

start total transfer timer


while base < total number of frames:

  while next < total AND next - base < window:

    serialize frame[next]
    pass frame through channel
    record sending time

    if frame is not lost:
      send frame

    increment frames_sent
    next = next + 1


  wait for message until timeout


  if connection is closed:
    report error


  if timeout occurs:

    increment timeouts


  else if message is ACK:

    parse ACK

    if ACK is valid:

      search outstanding frames from base to next - 1

      if an unacknowledged frame has matching sequence:

        mark that frame acknowledged

        calculate RTT
        update RTT timer

        increment acks_received

        stop searching


  while base < total AND frame[base] is acknowledged:

    base = base + 1


  current_time = now

  for each outstanding frame
    from base to next - 1:

    if frame is not acknowledged AND its timer has expired:

      retransmit only that frame

      update its sending time

      increment frames_sent
      increment retransmissions


send DONE

keep receiving messages until DONE_ACK arrives

calculate elapsed time

return statistics
*/

/*
RECEIVER:

check that window is between 1 and 128

create ACK channel
open output file

next_deliver = 0
buffer = empty


repeat:

  receive message

  if connection closes:
    report error

  if message is DONE:
    send DONE_ACK
    stop

  if message is not DATA:
    continue


  deserialize and CRC-check frame

  if frame is corrupted:
    continue


  distance = forward_distance(next_deliver, frame.sequence)


  if distance < window:

    store actual payload in buffer using sequence number as key

    send ACK for frame.sequence


    while buffer contains next_deliver:

      write buffered payload to output file

      remove it from buffer

      next_deliver = next_deliver + 1


    else:

      send ACK again for frame.sequence
*/