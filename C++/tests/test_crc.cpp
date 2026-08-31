#include "crc.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

std::vector<std::uint8_t> bytes(const std::string& text)
{
  return {text.begin(), text.end()};
}

int test_parameters()
{
  const bool match =
    flow_control::CRC8_PARAMETERS.width == 8U
    && flow_control::CRC8_PARAMETERS.polynomial == 0xD5U
    && flow_control::CRC10_PARAMETERS.width == 10U
    && flow_control::CRC10_PARAMETERS.polynomial == 0x233U
    && flow_control::CRC16_PARAMETERS.width == 16U
    && flow_control::CRC16_PARAMETERS.polynomial == 0x8005U
    && flow_control::CRC32_PARAMETERS.width == 32U
    && flow_control::CRC32_PARAMETERS.polynomial == 0x04C11DB7U;

  if (!match) {
    std::cerr << "FAIL: CRC parameter constants\n";
    return 1;
  }

  std::cout << "PASS: CRC parameter constants\n";
  return 0;
}

int test_known_vectors()
{
  const auto data = bytes("123456789");
  const bool match =
    flow_control::crc_compute(data, flow_control::CRC8_PARAMETERS)
      == 0xBCU
    && flow_control::crc_compute(data, flow_control::CRC10_PARAMETERS)
      == 0x199U
    && flow_control::crc_compute(data, flow_control::CRC16_PARAMETERS)
      == 0xFEE8U
    && flow_control::crc_compute(data, flow_control::CRC32_PARAMETERS)
      == 0x89A1897FU;

  if (!match) {
    std::cerr << "FAIL: CRC known vectors\n";
    return 1;
  }

  std::cout << "PASS: CRC known vectors\n";
  return 0;
}

int test_empty_input()
{
  if (flow_control::crc_compute({}, flow_control::CRC32_PARAMETERS) != 0U) {
    std::cerr << "FAIL: empty-input CRC\n";
    return 1;
  }

  std::cout << "PASS: empty-input CRC\n";
  return 0;
}

int test_verification()
{
  auto corrupted_data = bytes("123456789");
  corrupted_data[3] ^= 0x01U;

  const bool valid = flow_control::crc_verify(
    bytes("123456789"), 0xBCU, flow_control::CRC8_PARAMETERS
  );
  const bool rejects_data = !flow_control::crc_verify(
    corrupted_data, 0xBCU, flow_control::CRC8_PARAMETERS
  );
  const bool rejects_crc = !flow_control::crc_verify(
    bytes("123456789"), 0xBDU, flow_control::CRC8_PARAMETERS
  );

  if (!valid || !rejects_data || !rejects_crc) {
    std::cerr << "FAIL: CRC verification\n";
    return 1;
  }

  std::cout << "PASS: CRC verification\n";
  return 0;
}

int test_invalid_parameters()
{
  const std::vector<flow_control::CrcParameters> invalid{
    {0U, 0U},
    {33U, 0U},
    {8U, 0x1D5U}
  };

  for (const auto parameters : invalid) {
    try {
      (void)flow_control::crc_compute({}, parameters);
    } catch (const std::invalid_argument&) {
      continue;
    }

    std::cerr << "FAIL: invalid CRC parameters accepted\n";
    return 1;
  }

  std::cout << "PASS: invalid CRC parameter rejection\n";
  return 0;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += test_parameters();
  failures += test_known_vectors();
  failures += test_empty_input();
  failures += test_verification();
  failures += test_invalid_parameters();
  return failures == 0 ? 0 : 1;
}
