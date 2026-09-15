#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "channel.hpp"
#include "run_stats.hpp"
#include "socket.hpp"

namespace basic_flow {

/// Both windows are 'window' (1-128, to avoid ambiguity under modulo-256 sequence
/// wraparound). ACKs are independent per frame; only frames that individually
/// timeout are retransmitted.
RunStats run_selective_repeat_sender(const TcpSocket& socket, const std::vector<std::uint8_t>& file_bytes,
                                      int window, ChannelConfig data_channel_cfg, unsigned seed);

/// Buffers valid out-of-order frames within the receive window and ACKs every valid
/// frame individually; only corrupted frames are discarded (without an ACK). Payload
/// is written to 'output_path' only once contiguous.
void run_selective_repeat_receiver(const TcpSocket& socket, const std::string& output_path,
                                    int window, ChannelConfig ack_channel_cfg, unsigned seed);

}  // namespace basic_flow
