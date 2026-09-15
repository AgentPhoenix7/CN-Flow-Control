#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "channel.hpp"
#include "run_stats.hpp"
#include "socket.hpp"

namespace basic_flow {

/// Sender window is 'window' (1-255); on timeout the whole outstanding window is
/// retransmitted; ACKs are cumulative (an ACK for seq N acknowledges everything up
/// to and including N).
RunStats run_go_back_n_sender(const TcpSocket& socket, const std::vector<std::uint8_t>& file_bytes,
                               int window, ChannelConfig data_channel_cfg, unsigned seed);

/// Receiver window is fixed at 1: only the next in-order frame is accepted; anything
/// else (out-of-order or a duplicate) is discarded and re-ACKed with the last
/// in-order sequence so a lost cumulative ACK does not stall the sender.
void run_go_back_n_receiver(const TcpSocket& socket, const std::string& output_path,
                             ChannelConfig ack_channel_cfg, unsigned seed);

}  // namespace basic_flow
