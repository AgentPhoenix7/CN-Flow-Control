#include "go_back_n.hpp"

#include <chrono>
#include <fstream>
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

}  // namespace

RunStats run_go_back_n_sender(const TcpSocket& socket, const std::vector<std::uint8_t>& file_bytes, int window, ChannelConfig data_channel_cfg, unsigned seed) {
  if (window < 1 || window > 255) throw std::invalid_argument("go-back-n window must be 1-255");

  RunStats stats;
  Channel channel(data_channel_cfg, seed);
  RttTimer timer;
  auto frames = split_into_frames(file_bytes);
  const std::size_t total = frames.size();
  std::vector<Clock::time_point> sent_at(total);

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
    if (msg.status == RecvStatus::Timeout) {
      stats.timeouts++;
      for (std::size_t i = base; i < next; ++i) send_frame(i, true);
      continue;
    }
    if (msg.status == RecvStatus::Closed) {
      throw std::runtime_error("connection closed while waiting for ACK");
    }
    if (msg.tag != TAG_ACK) continue;
    auto ack_seq = parse_ack_body(msg.body);
    if (!ack_seq.has_value()) continue;  // corrupted ACK: ignore, keep waiting

    // Cumulative ACK: slide the window past the first outstanding frame whose
    // sequence number matches. An ACK for anything not in [base, next) is a stale
    // duplicate and is ignored.
    for (std::size_t i = base; i < next; ++i) {
      if (frames[i].seq == *ack_seq) {
        timer.on_sample(ms_between(sent_at[i], Clock::now()));
        stats.acks_received++;
        base = i + 1;
        break;
      }
    }
  }

  send_message(socket, TAG_DONE);
  // A spurious timeout near the end of the window can leave stray duplicate ACKs
  // ahead of the receiver's DONE_ACK in the stream; drain anything that isn't
  // DONE_ACK instead of failing on it. DONE_ACK itself is a control message, not
  // subject to channel impairment, so it is guaranteed to eventually arrive.
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

void run_go_back_n_receiver(const TcpSocket& socket, const std::string& output_path, ChannelConfig ack_channel_cfg, unsigned seed) {
  Channel ack_channel(ack_channel_cfg, seed);
  std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot open output file: " + output_path);

  std::uint8_t expected_seq = 0;
  bool have_delivered = false;

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

    if (frame.seq == expected_seq) {
      out.write(reinterpret_cast<const char*>(frame.payload.data()), frame.length);
      have_delivered = true;
      expected_seq = static_cast<std::uint8_t>(expected_seq + 1);
      send_ack(static_cast<std::uint8_t>(expected_seq - 1));  // cumulative: what's now delivered
    } else if (have_delivered) {
      // Out-of-order or duplicate: discard, re-ACK the last in-order delivery.
      send_ack(static_cast<std::uint8_t>(expected_seq - 1));
    }
  }
}

}  // namespace basic_flow

/*
SENDER:

check that window is between 1 and 255

create channel
create RTT timer
split file into frames

create sending-time array

base = 0
next = 0

start total transfer timer


while base < total number of frames:

  while next < total AND next - base < window:

    serialize frame[next]
    pass it through channel
    record its sending time

    if frame is not lost:
      send frame

    increment frames_sent
    next = next + 1


  wait for ACK until timeout


  if timeout occurs:

    increment timeouts

    for every frame from base to next - 1:
      retransmit frame
      update its sending time
      increment frames_sent
      increment retransmissions

    continue


  if connection is closed:
    report error


  if message is not ACK:
    continue


  parse ACK

  if ACK is invalid:
    continue


  search outstanding frames from base to next - 1

  if frame sequence matches ACK:

    calculate RTT
    update RTT timer

    increment acks_received

    base = matched frame index + 1

    stop searching


send DONE

keep receiving messages until DONE_ACK arrives

calculate elapsed time

return statistics
*/

/*
RECEIVER:

create ACK channel
open output file

expected_sequence = 0
have_delivered = false


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


  if frame.sequence == expected_sequence:

    write actual payload bytes to file

    have_delivered = true

    expected_sequence = expected_sequence + 1

    send ACK for expected_sequence - 1


  else if at least one frame was already delivered:

    discard received frame

    send ACK again for expected_sequence - 1
*/