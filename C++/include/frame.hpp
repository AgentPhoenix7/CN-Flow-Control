/**
 * @file frame.hpp
 * @brief Declares protected data-frame serialization and verification.
 */

#ifndef FLOW_CONTROL_FRAME_HPP
#define FLOW_CONTROL_FRAME_HPP

#include "config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace flow_control
{

/** Supported frame-check-sequence schemes. */
enum class FcsScheme : std::uint8_t
{
  Checksum16,
  Crc8,
  Crc10,
  Crc16,
  Crc32
};

/** Logical fields in the manually serialized 15-byte header. */
struct FrameHeader
{
  std::array<std::uint8_t, MAC_ADDRESS_SIZE> source;
  std::array<std::uint8_t, MAC_ADDRESS_SIZE> destination;
  std::uint16_t valid_payload_length;
  std::uint8_t sequence;
};

/** A structurally valid frame whose FCS has been verified. */
struct VerifiedFrame
{
  FrameHeader header;
  std::vector<std::uint8_t> payload;
};

/** @brief Returns the number of wire bytes used by a scheme's FCS. */
std::size_t fcs_size(FcsScheme scheme);

/**
 * @brief Serializes a canonical protected frame with zero padding.
 * @throws std::invalid_argument for a length mismatch or oversized payload.
 */
std::vector<std::uint8_t> serialize_frame(
  const FrameHeader& header,
  const std::vector<std::uint8_t>& payload,
  FcsScheme scheme
);

/**
 * @brief Verifies structure, canonical padding, and FCS before extracting data.
 * @return A verified frame, or std::nullopt when any check fails.
 */
std::optional<VerifiedFrame> verify_frame(
  const std::vector<std::uint8_t>& wire,
  FcsScheme scheme
);

}  // namespace flow_control

#endif
