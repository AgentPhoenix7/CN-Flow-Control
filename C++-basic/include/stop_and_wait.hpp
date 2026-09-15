#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "channel.hpp"
#include "run_stats.hpp"
#include "socket.hpp"

namespace basic_flow {

/// One outstanding frame at a time: sends it, waits for the matching ACK or a
/// timeout, retransmits on timeout, moves on once acknowledged.
RunStats run_stop_and_wait_sender(const TcpSocket& socket, const std::vector<std::uint8_t>& file_bytes,
                                   ChannelConfig data_channel_cfg, unsigned seed);

/// Accepts frames in strict sequence order, discards corrupted/out-of-order frames
/// (re-ACKing the last good delivery so a lost ACK does not stall the sender), and
/// writes payload to 'output_path' as each frame is accepted.
void run_stop_and_wait_receiver(const TcpSocket& socket, const std::string& output_path,
                                 ChannelConfig ack_channel_cfg, unsigned seed);

}  // namespace basic_flow
