#pragma once

#include <cstdint>
#include <random>
#include <vector>

namespace basic_flow {

/// One path's (DATA or ACK) simulated impairment: a probability of a single-bit
/// corruption and an independent probability of the message being dropped (the
/// PDF's "random delay ... causes packet loss or timeout" -- excessive delay and
/// loss are indistinguishable to a timer, so one probability models both).
struct ChannelConfig {
  double error_prob = 0.0;
  double loss_prob = 0.0;
};

/// Applies one path's configured probabilities to outgoing message bodies. Each
/// direction (sender's DATA path, receiver's ACK path) owns its own Channel and RNG
/// state so the two paths never share randomness.
class Channel {
public:
  Channel(ChannelConfig config, unsigned seed);

  /// Decides the fate of one outgoing message body. Returns false if it must be
  /// dropped (the caller must not put anything on the wire this round). If it
  /// returns true, 'body' may have been corrupted in place (one random bit flipped).
  bool apply(std::vector<std::uint8_t>& body);

private:
  ChannelConfig config_;
  std::mt19937 rng_;
};

}  // namespace basic_flow
