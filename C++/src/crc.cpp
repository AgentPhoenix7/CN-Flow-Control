#include "crc.hpp"

#include <cstdint>
#include <stdexcept>

namespace flow_control
{

const CrcParameters CRC8_PARAMETERS{8U, 0xD5U};
const CrcParameters CRC10_PARAMETERS{10U, 0x233U};
const CrcParameters CRC16_PARAMETERS{16U, 0x8005U};
const CrcParameters CRC32_PARAMETERS{32U, 0x04C11DB7U};

namespace
{

std::uint32_t width_mask(std::uint8_t width)
{
  if (width == 0U || width > 32U) {
    throw std::invalid_argument("CRC width must be between 1 and 32");
  }

  if (width == 32U) {
    return UINT32_MAX;
  }

  return (std::uint32_t{1U} << width) - 1U;
}

}  // namespace

std::uint32_t crc_compute(
  const std::vector<std::uint8_t>& data,
  CrcParameters parameters
)
{
  const std::uint32_t mask = width_mask(parameters.width);

  if ((parameters.polynomial & ~mask) != 0U) {
    throw std::invalid_argument("CRC polynomial exceeds its width");
  }

  const std::uint32_t top_bit =
    std::uint32_t{1U} << (parameters.width - 1U);
  std::uint32_t remainder = 0U;

  for (const std::uint8_t byte : data) {
    for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
      const bool data_bit =
        (byte & static_cast<std::uint8_t>(0x80U >> bit)) != 0U;
      const bool remainder_bit = (remainder & top_bit) != 0U;
      remainder = (remainder << 1U) & mask;

      if (data_bit != remainder_bit) {
        remainder ^= parameters.polynomial;
      }
    }
  }

  return remainder & mask;
}

bool crc_verify(
  const std::vector<std::uint8_t>& data,
  std::uint32_t received_crc,
  CrcParameters parameters
)
{
  const std::uint32_t mask = width_mask(parameters.width);

  if ((received_crc & ~mask) != 0U) {
    return false;
  }

  return crc_compute(data, parameters) == received_crc;
}

}  // namespace flow_control
