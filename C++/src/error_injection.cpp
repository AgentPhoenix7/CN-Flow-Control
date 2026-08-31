#include "error_injection.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace flow_control
{

ErrorInjectionRng::ErrorInjectionRng(std::uint32_t seed) noexcept
  : state_{seed}
{
}

std::uint32_t ErrorInjectionRng::next() noexcept
{
  state_ = state_ * 1664525U + 1013904223U;
  return state_;
}

std::uint32_t ErrorInjectionRng::state() const noexcept
{
  return state_;
}

bool select_probability(ErrorInjectionRng& rng, double probability)
{
  if (!std::isfinite(probability)
      || probability < 0.0
      || probability > 1.0) {
    throw std::invalid_argument("probability must be finite and in [0, 1]");
  }

  if (probability == 0.0) {
    return false;
  }

  if (probability == 1.0) {
    return true;
  }

  constexpr double RANGE = 4294967296.0;
  return static_cast<double>(rng.next()) / RANGE < probability;
}

bool flip_bit(
  std::vector<std::uint8_t>& data,
  std::size_t bit_index
) noexcept
{
  const std::size_t byte_index = bit_index / 8U;

  if (byte_index >= data.size()) {
    return false;
  }

  const std::size_t bit_offset = bit_index % 8U;
  const auto mask = static_cast<std::uint8_t>(0x80U >> bit_offset);
  data[byte_index] ^= mask;
  return true;
}

bool flip_burst(
  std::vector<std::uint8_t>& data,
  std::size_t start_bit_index,
  std::size_t burst_length
) noexcept
{
  if (burst_length == 0U
      || burst_length - 1U
        > std::numeric_limits<std::size_t>::max() - start_bit_index) {
    return false;
  }

  const std::size_t final_bit_index =
    start_bit_index + burst_length - 1U;

  if (final_bit_index / 8U >= data.size()) {
    return false;
  }

  for (std::size_t offset = 0U; offset < burst_length; ++offset) {
    (void)flip_bit(data, start_bit_index + offset);
  }

  return true;
}

bool flip_random_bit(
  std::vector<std::uint8_t>& data,
  ErrorInjectionRng& rng
) noexcept
{
  if (data.empty()
      || data.size() > std::numeric_limits<std::size_t>::max() / 8U) {
    return false;
  }

  const std::size_t bit_count = data.size() * 8U;
  const std::size_t bit_index =
    static_cast<std::size_t>(rng.next()) % bit_count;
  return flip_bit(data, bit_index);
}

}  // namespace flow_control
