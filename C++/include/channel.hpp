/**
 * @file channel.hpp
 * @brief Declares deterministic application-layer channel impairment.
 */

#ifndef FLOW_CONTROL_CHANNEL_HPP
#define FLOW_CONTROL_CHANNEL_HPP

#include "error_injection.hpp"

#include <cstdint>
#include <vector>

namespace flow_control
{

/** Mutually exclusive result of one channel transmission attempt. */
enum class ChannelOutcome
{
  Clean,
  Dropped,
  Delayed,
  Corrupted
};

/** Independent impairment probabilities and reproducibility seeds. */
struct ChannelConfig
{
  double drop_probability;
  double delay_probability;
  double corruption_probability;
  std::uint32_t selection_seed;
  std::uint32_t bit_seed;
};

/** Bytes delivered by the simulated channel and their outcome. */
struct ChannelResult
{
  ChannelOutcome outcome;
  std::vector<std::uint8_t> bytes;
};

/** Applies seeded drop, excessive-delay, and corruption decisions. */
class Channel
{
public:
  /** @throws std::invalid_argument for a non-finite/out-of-range probability. */
  explicit Channel(ChannelConfig config);

  /** Simulates one transmission without modifying the caller's bytes. */
  ChannelResult transmit(const std::vector<std::uint8_t>& bytes);

private:
  ChannelConfig config_;
  ErrorInjectionRng selection_rng_;
  ErrorInjectionRng bit_rng_;
};

}  // namespace flow_control

#endif
