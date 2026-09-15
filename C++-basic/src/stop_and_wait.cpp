#include "stop_and_wait.hpp"

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

RunStats run_stop_and_wait_sender(const TcpSocket& socket, const std::vector<std::uint8_t>& file_bytes,
                                   ChannelConfig data_channel_cfg, unsigned seed) {
  RunStats stats;
  Channel channel(data_channel_cfg, seed);
  RttTimer timer;
  auto frames = split_into_frames(file_bytes);
  auto start = Clock::now();

  for (const auto& frame : frames) {
    bool acked = false;
    while (!acked) {
      auto body = serialize(frame);
      bool delivered = channel.apply(body);
      auto sent_at = Clock::now();
      if (delivered) send_message(socket, TAG_DATA, body);
      stats.frames_sent++;

      auto msg = try_recv_message(socket, static_cast<int>(timer.timeout_ms()));
      if (msg.status == RecvStatus::Timeout) {
        stats.timeouts++;
        stats.retransmissions++;
        continue;
      }
      if (msg.status == RecvStatus::Closed) {
        throw std::runtime_error("connection closed while waiting for ACK");
      }
      if (msg.tag != TAG_ACK) continue;

      auto ack_seq = parse_ack_body(msg.body);
      if (!ack_seq.has_value() || *ack_seq != frame.seq) continue;  // corrupted or stale ACK

      timer.on_sample(ms_between(sent_at, Clock::now()));
      stats.acks_received++;
      acked = true;
    }
  }

  send_message(socket, TAG_DONE);
  // A spurious timeout on the very last frame can leave one stray duplicate ACK
  // ahead of the receiver's DONE_ACK in the stream (the retransmitted frame's own
  // re-ACK); drain anything that isn't DONE_ACK instead of failing on it. DONE_ACK
  // itself is a control message, not subject to channel impairment, so it is
  // guaranteed to eventually arrive.
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

void run_stop_and_wait_receiver(const TcpSocket& socket, const std::string& output_path, ChannelConfig ack_channel_cfg, unsigned seed) {
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
      send_ack(frame.seq);
      have_delivered = true;
      expected_seq = static_cast<std::uint8_t>(expected_seq + 1);
    } else if (have_delivered) {
      // Duplicate retransmission of the frame already delivered: re-ACK it, don't
      // write its payload a second time.
      send_ack(static_cast<std::uint8_t>(expected_seq - 1));
    }
  }
}

}  // namespace basic_flow

/*
SENDER:

create channel
create RTT timer
split file into frames

start timer for total transfer

for each frame:

  acknowledged = false

  while acknowledged == false:

    serialize frame

    pass frame through simulated channel

    record sending time

    if frame is not lost:
      send frame

    increment frames_sent

    wait for message until timeout

    if timeout occurs:
      increment timeouts
      increment retransmissions
      continue

    if connection is closed:
      report error

    if message is not an ACK:
      continue

    parse ACK

    if ACK is invalid:
      continue

    if ACK sequence != current frame sequence:
      continue

    calculate RTT
    update RTT timer

    increment acks_received
    acknowledged = true


send DONE

repeat:
  receive message without timeout

  if connection closes:
    report error

until message is DONE_ACK

calculate total elapsed time

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

  if connection is closed:
    report error

  if message is DONE:
    send DONE_ACK
    stop

  if message is not DATA:
    continue

  deserialize frame and verify CRC

  if frame is corrupted:
    continue

  if frame.sequence == expected_sequence:

    write actual payload bytes to file

    send ACK for frame.sequence

    have_delivered = true

    expected_sequence = expected_sequence + 1

  else if at least one frame was already delivered:

    send ACK for previous expected sequence
*/