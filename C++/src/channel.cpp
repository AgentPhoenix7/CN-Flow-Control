#include "channel.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace flow_control
{

namespace
{

bool valid_probability(double probability)
{
  return std::isfinite(probability)
    && probability >= 0.0
    && probability <= 1.0;
}

}  // namespace

Channel::Channel(ChannelConfig config)
  : config_{config},
    selection_rng_{config.selection_seed},
    bit_rng_{config.bit_seed}
{
  if (!valid_probability(config.drop_probability)
      || !valid_probability(config.delay_probability)
      || !valid_probability(config.corruption_probability)) {
    throw std::invalid_argument(
      "channel probabilities must be finite and in [0, 1]"
    );
  }
}

ChannelResult Channel::transmit(const std::vector<std::uint8_t>& bytes)
{
  if (select_probability(selection_rng_, config_.drop_probability)) {
    return {ChannelOutcome::Dropped, {}};
  }

  if (select_probability(selection_rng_, config_.delay_probability)) {
    return {ChannelOutcome::Delayed, {}};
  }

  std::vector<std::uint8_t> delivered = bytes;
  if (select_probability(selection_rng_, config_.corruption_probability)) {
    if (!flip_random_bit(delivered, bit_rng_)) {
      return {ChannelOutcome::Dropped, {}};
    }
    return {ChannelOutcome::Corrupted, std::move(delivered)};
  }

  return {ChannelOutcome::Clean, std::move(delivered)};
}

}  // namespace flow_control
