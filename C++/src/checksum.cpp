#include "checksum.hpp"

#include <cstddef>
#include <cstdint>

namespace flow_control
{

std::uint16_t checksum16_compute(
  const std::vector<std::uint8_t>& data
)
{
  constexpr std::uint32_t WORD_MASK = 0xFFFFU;
  std::uint32_t sum = 0U;

  for (
    std::size_t index = 0U;
    index + 1U < data.size();
    index += 2U
  ) {
    const std::uint32_t word =
      (static_cast<std::uint32_t>(data[index]) << 8U)
      | static_cast<std::uint32_t>(data[index + 1U]);

    sum += word;
    sum = (sum & WORD_MASK) + (sum >> 16U);
  }

  return static_cast<std::uint16_t>(~sum);
}

}  // namespace flow_control
