/**
 * @file checksum.hpp
 * @brief Declares reusable 16-bit one's-complement checksum operations.
 */

#ifndef FLOW_CONTROL_CHECKSUM_HPP
#define FLOW_CONTROL_CHECKSUM_HPP

#include <cstdint>
#include <vector>

namespace flow_control
{

/**
 * @brief Calculates the 16-bit one's-complement checksum of a byte sequence.
 *
 * Adjacent bytes form big-endian 16-bit words. For odd-length input, the
 * final byte is the high byte of a word whose low byte is zero.
 *
 * @param data Input bytes.
 * @return The calculated 16-bit checksum.
 */
std::uint16_t checksum16_compute(
  const std::vector<std::uint8_t>& data
);

}  // namespace flow_control

#endif
