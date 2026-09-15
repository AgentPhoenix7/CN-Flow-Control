#pragma once

#include <cstdint>
#include <vector>

namespace basic_flow {

/// Computes a CRC-16 (poly 0x8005, init 0x0000, MSB-first, bit-serial) over 'data'.
/// The sender computes this over header+payload to build the trailer; the receiver
/// recomputes it over the same bytes to verify the frame.
std::uint16_t crc16_compute(const std::vector<std::uint8_t>& data);

}  // namespace basic_flow
