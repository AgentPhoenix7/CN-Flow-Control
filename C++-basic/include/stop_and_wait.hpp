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

/// Verifies the four-byte CRC-32 FCS of each 65-byte DATA body; corrupted frames
/// receive no ACK. Accepts frames in strict sequence order and re-ACKs the last
/// good delivery on a valid duplicate. Writes each accepted payload only once.
void run_stop_and_wait_receiver(const TcpSocket& socket, const std::string& output_path,
                                 ChannelConfig ack_channel_cfg, unsigned seed);

}  // namespace basic_flow
