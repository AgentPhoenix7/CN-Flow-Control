#include "channel.hpp"

namespace basic_flow {

Channel::Channel(ChannelConfig config, unsigned seed) : config_(config), rng_(seed) {}

bool Channel::apply(std::vector<std::uint8_t>& body) {
  std::uniform_real_distribution<double> uniform(0.0, 1.0);

  if (uniform(rng_) < config_.loss_prob) {
    return false;
  }
  if (!body.empty() && uniform(rng_) < config_.error_prob) {
    std::uniform_int_distribution<std::size_t> byte_pick(0, body.size() - 1);
    std::uniform_int_distribution<int> bit_pick(0, 7);
    body[byte_pick(rng_)] ^= static_cast<std::uint8_t>(1u << bit_pick(rng_));
  }
  return true;
}

}  // namespace basic_flow

/*
generate random number between 0 and 1

if random number < loss probability:
  return false

if body is not empty:
  generate another random number

  if random number < error probability:
    choose a random byte
    choose a random bit from 0 to 7

    flip that bit in the chosen byte

return true
*/