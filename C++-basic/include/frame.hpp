#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace basic_flow {

constexpr std::size_t MAC_LEN = 6;
constexpr std::size_t HEADER_LEN = MAC_LEN + MAC_LEN + 2 + 1;  // 15 bytes
constexpr std::size_t PAYLOAD_LEN = 46;                        // fixed, per the assignment's own example
constexpr std::size_t CRC_LEN = 4;                              // four-byte CRC-32 FCS, per assignment
constexpr std::size_t FRAME_BODY_LEN = HEADER_LEN + PAYLOAD_LEN + CRC_LEN;  // 65 bytes

/// One data frame: 15-byte header (source MAC, destination MAC, unpadded payload
/// length, sequence number), a zero-padded 46-byte payload, and a 4-byte CRC-32
/// trailer. Fields are serialized individually -- never a raw memory copy of this
/// struct -- so the wire layout does not depend on struct padding/alignment.
/// The simulated frame is 65 bytes; wire.hpp adds a separate one-byte tag (66 total).
struct DataFrame {
  std::array<std::uint8_t, MAC_LEN> src_mac{};
  std::array<std::uint8_t, MAC_LEN> dst_mac{};
  std::uint16_t length = 0;  ///< true unpadded payload length, 0..PAYLOAD_LEN
  std::uint8_t seq = 0;      ///< sequence number, wraps modulo 256
  std::array<std::uint8_t, PAYLOAD_LEN> payload{};
};

/// Serializes header + zero-padded payload + CRC-32 into a fixed FRAME_BODY_LEN buffer.
std::vector<std::uint8_t> serialize(const DataFrame& frame);

/// Parses a FRAME_BODY_LEN buffer and verifies its CRC-32. Returns false (and leaves
/// 'out' unspecified) if the buffer is the wrong size, the length field is invalid,
/// or the CRC does not match -- the caller must discard the frame and send no ACK.
bool deserialize_and_verify(const std::vector<std::uint8_t>& body, DataFrame& out);

/// Splits a file's bytes into PAYLOAD_LEN-sized chunks, numbering sequence numbers
/// 0, 1, 2, ... modulo 256. A short final chunk is zero-padded to 46 payload bytes;
/// its length field records the useful byte count. Every serialized frame is 65 bytes.
std::vector<DataFrame> split_into_frames(const std::vector<std::uint8_t>& file_bytes);

}  // namespace basic_flow
