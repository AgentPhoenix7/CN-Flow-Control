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

/// Checks each 65-byte DATA body's four-byte CRC-32 FCS, discarding failures without
/// an ACK. Buffers and ACKs valid frames within the receive window independently;
/// re-ACKs already-delivered duplicates within the preceding window. Payload is
/// written to 'output_path' only once contiguous.
void run_selective_repeat_receiver(const TcpSocket& socket, const std::string& output_path,
                                    int window, ChannelConfig ack_channel_cfg, unsigned seed);

}  // namespace basic_flow
