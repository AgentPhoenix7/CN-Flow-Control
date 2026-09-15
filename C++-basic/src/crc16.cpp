#include "crc16.hpp"

namespace basic_flow {

std::uint16_t crc16_compute(const std::vector<std::uint8_t>& data) {
  constexpr std::uint16_t kPoly = 0x8005;
  std::uint16_t crc = 0x0000;
  for (std::uint8_t byte : data) {
    crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(byte) << 8));
    for (int bit = 0; bit < 8; ++bit) {
      if (crc & 0x8000) {
        crc = static_cast<std::uint16_t>((crc << 1) ^ kPoly);
      } else {
        crc = static_cast<std::uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

}  // namespace basic_flow

/*
crc = 0

for each byte:
  put byte into upper part of crc using XOR

  repeat 8 times:
    if highest bit of crc == 1:
      crc = (crc << 1) XOR polynomial
    else:
      crc = crc << 1

return crc
*/

/*
crc -> frame -> channel -> timer -> stop_and_wait / go_back_n / selective_repeat -> wire -> socket -> sender_main -> receiver_main
*/
