#include "crc32.hpp"

namespace basic_flow {

std::uint32_t crc32_compute(const std::vector<std::uint8_t>& data) {
  constexpr std::uint32_t kPoly = 0xEDB88320u;
  std::uint32_t crc = 0xFFFFFFFFu;
  for (const std::uint8_t byte : data) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      if (crc & 1u) {
        crc = (crc >> 1) ^ kPoly;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

}  // namespace basic_flow

/*
CRC-32/ISO-HDLC over the supplied bytes (the FCS itself is excluded):

crc = 0xFFFFFFFF
for each byte:
  XOR byte into the low 8 bits of crc
  repeat 8 times:
    if lowest bit is 1: crc = (crc >> 1) XOR 0xEDB88320
    else: crc = crc >> 1
return crc XOR 0xFFFFFFFF

The frame stores the resulting four-byte number in network byte order.
*/
