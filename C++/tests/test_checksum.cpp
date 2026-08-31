#include "checksum.hpp"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace
{

int test_even_length_checksum()
{
  const std::vector<std::uint8_t> data{
    0x00U, 0x01U, 0xF2U, 0x03U,
    0xF4U, 0xF5U, 0xF6U, 0xF7U
  };
  constexpr std::uint16_t expected = 0x220DU;

  const std::uint16_t actual =
    flow_control::checksum16_compute(data);

  if (actual != expected) {
    std::cerr
      << "FAIL: expected 0x"
      << std::hex << std::uppercase << std::setw(4)
      << std::setfill('0') << expected
      << ", received 0x" << std::setw(4) << actual
      << '\n';
    return 1;
  }

  std::cout << "PASS: even-length checksum\n";
  return 0;
}

int test_odd_length_checksum()
{
  const std::vector<std::uint8_t> data{
    0x12U, 0x34U, 0x56U
  };
  constexpr std::uint16_t expected = 0x97CBU;

  const std::uint16_t actual =
    flow_control::checksum16_compute(data);

  if (actual != expected) {
    std::cerr
      << "FAIL odd-length: expected 0x"
      << std::hex << std::uppercase << std::setw(4)
      << std::setfill('0') << expected
      << ", received 0x" << std::setw(4) << actual
      << '\n';
    return 1;
  }

  std::cout << "PASS: odd-length checksum\n";
  return 0;
}

int test_empty_input_checksum()
{
  const std::vector<std::uint8_t> data{};
  constexpr std::uint16_t expected = 0xFFFFU;

  const std::uint16_t actual =
    flow_control::checksum16_compute(data);

  if (actual != expected) {
    std::cerr
      << "FAIL empty input: expected 0x"
      << std::hex << std::uppercase << std::setw(4)
      << std::setfill('0') << expected
      << ", received 0x" << std::setw(4) << actual
      << '\n';
    return 1;
  }

  std::cout << "PASS: empty-input checksum\n";
  return 0;
}

int test_valid_checksum_verification()
{
  const std::vector<std::uint8_t> data{
    0x00U, 0x01U, 0xF2U, 0x03U,
    0xF4U, 0xF5U, 0xF6U, 0xF7U
  };
  constexpr std::uint16_t received_checksum = 0x220DU;

  if (!flow_control::checksum16_verify(
        data,
        received_checksum
      )) {
    std::cerr << "FAIL: valid checksum was rejected\n";
    return 1;
  }

  std::cout << "PASS: valid checksum verification\n";
  return 0;
}

int test_corrupted_payload_rejection()
{
  const std::vector<std::uint8_t> corrupted_data{
    0x00U, 0x01U, 0xF2U, 0x03U,
    0xF4U, 0xF5U, 0xF6U, 0xF6U
  };
  constexpr std::uint16_t original_checksum = 0x220DU;

  if (flow_control::checksum16_verify(
        corrupted_data,
        original_checksum
      )) {
    std::cerr << "FAIL: corrupted payload was accepted\n";
    return 1;
  }

  std::cout << "PASS: corrupted-payload rejection\n";
  return 0;
}

int test_corrupted_checksum_rejection()
{
  const std::vector<std::uint8_t> data{
    0x00U, 0x01U, 0xF2U, 0x03U,
    0xF4U, 0xF5U, 0xF6U, 0xF7U
  };
  constexpr std::uint16_t corrupted_checksum = 0x220CU;

  if (flow_control::checksum16_verify(
        data,
        corrupted_checksum
      )) {
    std::cerr << "FAIL: corrupted checksum was accepted\n";
    return 1;
  }

  std::cout << "PASS: corrupted-checksum rejection\n";
  return 0;
}

}  // namespace

int main()
{
  int failures = 0;

  failures += test_even_length_checksum();
  failures += test_odd_length_checksum();
  failures += test_empty_input_checksum();
  failures += test_valid_checksum_verification();
  failures += test_corrupted_payload_rejection();
  failures += test_corrupted_checksum_rejection();

  return failures == 0 ? 0 : 1;
}
