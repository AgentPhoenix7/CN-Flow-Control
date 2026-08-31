/**
 * @file error_injection.hpp
 * @brief Declares deterministic bit mutation and selection operations.
 */

#ifndef FLOW_CONTROL_ERROR_INJECTION_HPP
#define FLOW_CONTROL_ERROR_INJECTION_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace flow_control
{

/** Reproducible 32-bit linear-congruential generator state. */
class ErrorInjectionRng
{
public:
  /** Initializes the generator with an explicit seed. */
  explicit ErrorInjectionRng(std::uint32_t seed) noexcept;

  /** Advances and returns the next pseudo-random value. */
  std::uint32_t next() noexcept;

  /** Returns the current generator state without advancing it. */
  std::uint32_t state() const noexcept;

private:
  std::uint32_t state_;
};

/**
 * @brief Selects an event with the requested probability.
 * @throws std::invalid_argument unless probability is finite and in [0, 1].
 */
bool select_probability(ErrorInjectionRng& rng, double probability);

/** @brief Flips one MSB-indexed bit, returning false for an invalid index. */
bool flip_bit(
  std::vector<std::uint8_t>& data,
  std::size_t bit_index
) noexcept;

/**
 * @brief Flips a complete contiguous MSB-indexed range atomically.
 * @return false without mutation when the range is empty, overflows, or is
 * outside the data.
 */
bool flip_burst(
  std::vector<std::uint8_t>& data,
  std::size_t start_bit_index,
  std::size_t burst_length
) noexcept;

/** @brief Flips one reproducibly selected bit. */
bool flip_random_bit(
  std::vector<std::uint8_t>& data,
  ErrorInjectionRng& rng
) noexcept;

}  // namespace flow_control

#endif
