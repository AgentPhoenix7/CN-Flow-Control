#pragma once

#include <cstdint>
#include <vector>

namespace basic_flow {

/// Computes CRC-32/ISO-HDLC: reflected poly 0xEDB88320 (normal 0x04C11DB7),
/// init 0xFFFFFFFF, reflected input/output, final XOR 0xFFFFFFFF.
/// Check value for "123456789": 0xCBF43926; empty input: 0x00000000.
/// The sender computes this over the 15-byte header plus all 46 padded payload bytes;
/// frame.cpp writes the numeric result as a four-byte FCS in network byte order.
/// The receiver recomputes it over the same 61 bytes and compares all four FCS bytes.
std::uint32_t crc32_compute(const std::vector<std::uint8_t>& data);

}  // namespace basic_flow
