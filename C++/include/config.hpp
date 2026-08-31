/**
 * @file config.hpp
 * @brief Defines fixed framing and protocol limits.
 */

#ifndef FLOW_CONTROL_CONFIG_HPP
#define FLOW_CONTROL_CONFIG_HPP

#include <cstddef>

namespace flow_control
{

/** Number of bytes in one MAC address. */
inline constexpr std::size_t MAC_ADDRESS_SIZE = 6U;
/** Serialized source, destination, length, and sequence header size. */
inline constexpr std::size_t FRAME_HEADER_SIZE =
  (2U * MAC_ADDRESS_SIZE) + 2U + 1U;
/** Minimum complete simulated frame size. */
inline constexpr std::size_t MIN_FRAME_SIZE = 64U;
/** Maximum complete simulated frame size. */
inline constexpr std::size_t MAX_FRAME_SIZE = 1518U;

/** Checksum-16 field size. */
inline constexpr std::size_t CHECKSUM16_SIZE = 2U;
/** CRC-8 field size. */
inline constexpr std::size_t CRC8_SIZE = 1U;
/** CRC-10 field size. */
inline constexpr std::size_t CRC10_SIZE = 2U;
/** CRC-16 field size. */
inline constexpr std::size_t CRC16_SIZE = 2U;
/** CRC-32 field size. */
inline constexpr std::size_t CRC32_SIZE = 4U;

/** Largest payload safe for every supported FCS scheme. */
inline constexpr std::size_t MAX_PAYLOAD_SIZE =
  MAX_FRAME_SIZE - FRAME_HEADER_SIZE - CRC32_SIZE;
/** Assignment-recommended payload size used by default. */
inline constexpr std::size_t DEFAULT_PAYLOAD_SIZE = 46U;

static_assert(FRAME_HEADER_SIZE == 15U);
static_assert(
  FRAME_HEADER_SIZE + MAX_PAYLOAD_SIZE + CRC32_SIZE
  == MAX_FRAME_SIZE
);
static_assert(
  DEFAULT_PAYLOAD_SIZE > 0U
  && DEFAULT_PAYLOAD_SIZE <= MAX_PAYLOAD_SIZE
);

}  // namespace flow_control

#endif
