/**
 * @file crc.hpp
 * @brief Declares generic MSB-first CRC operations.
 */

#ifndef FLOW_CONTROL_CRC_HPP
#define FLOW_CONTROL_CRC_HPP

#include <cstdint>
#include <vector>

namespace flow_control
{

/** Describes a CRC remainder width and polynomial without its top term. */
struct CrcParameters
{
  std::uint8_t width;
  std::uint32_t polynomial;
};

/** CRC-8 parameters using polynomial 0xD5. */
extern const CrcParameters CRC8_PARAMETERS;
/** CRC-10 parameters using polynomial 0x233. */
extern const CrcParameters CRC10_PARAMETERS;
/** CRC-16 parameters using polynomial 0x8005. */
extern const CrcParameters CRC16_PARAMETERS;
/** CRC-32 parameters using polynomial 0x04C11DB7. */
extern const CrcParameters CRC32_PARAMETERS;

/**
 * @brief Computes an unreflected, zero-initialized, MSB-first CRC.
 * @throws std::invalid_argument when width is outside 1--32 or the
 * polynomial does not fit that width.
 */
std::uint32_t crc_compute(
  const std::vector<std::uint8_t>& data,
  CrcParameters parameters
);

/** @brief Returns whether data has the supplied CRC remainder. */
bool crc_verify(
  const std::vector<std::uint8_t>& data,
  std::uint32_t received_crc,
  CrcParameters parameters
);

}  // namespace flow_control

#endif
