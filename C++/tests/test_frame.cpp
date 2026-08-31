#include "config.hpp"
#include "frame.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{

flow_control::FrameHeader header(std::uint16_t length, std::uint8_t sequence)
{
  return {
    {0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U},
    {0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU},
    length,
    sequence
  };
}

int test_manual_header_layout()
{
  const std::vector<std::uint8_t> payload{0x10U, 0x20U, 0x30U};
  const auto wire = flow_control::serialize_frame(
    header(3U, 0x7EU), payload, flow_control::FcsScheme::Checksum16
  );

  const std::vector<std::uint8_t> expected_header{
    0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U,
    0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU,
    0x00U, 0x03U, 0x7EU
  };

  if (!std::equal(expected_header.begin(), expected_header.end(), wire.begin())) {
    std::cerr << "FAIL: manual frame header layout\n";
    return 1;
  }

  std::cout << "PASS: manual frame header layout\n";
  return 0;
}

int test_scheme_padding_sizes()
{
  const std::vector<std::uint8_t> payload(46U, 0x5AU);
  const auto crc8 = flow_control::serialize_frame(
    header(46U, 1U), payload, flow_control::FcsScheme::Crc8
  );
  const auto checksum = flow_control::serialize_frame(
    header(46U, 1U), payload, flow_control::FcsScheme::Checksum16
  );
  const auto crc32 = flow_control::serialize_frame(
    header(46U, 1U), payload, flow_control::FcsScheme::Crc32
  );

  if (crc8.size() != flow_control::MIN_FRAME_SIZE
      || checksum.size() != flow_control::MIN_FRAME_SIZE
      || crc32.size() != 65U) {
    std::cerr << "FAIL: scheme-dependent frame padding\n";
    return 1;
  }

  std::cout << "PASS: scheme-dependent frame padding\n";
  return 0;
}

int test_all_scheme_round_trips()
{
  const std::vector<flow_control::FcsScheme> schemes{
    flow_control::FcsScheme::Checksum16,
    flow_control::FcsScheme::Crc8,
    flow_control::FcsScheme::Crc10,
    flow_control::FcsScheme::Crc16,
    flow_control::FcsScheme::Crc32
  };
  const std::vector<std::uint8_t> payload{0xDEU, 0xADU, 0xBEU, 0xEFU};

  for (const auto scheme : schemes) {
    const auto wire = flow_control::serialize_frame(
      header(4U, 255U), payload, scheme
    );
    const auto parsed = flow_control::verify_frame(wire, scheme);

    if (!parsed.has_value()
        || parsed->header.valid_payload_length != 4U
        || parsed->header.sequence != 255U
        || parsed->payload != payload) {
      std::cerr << "FAIL: FCS frame round trip\n";
      return 1;
    }
  }

  std::cout << "PASS: all-FCS frame round trips\n";
  return 0;
}

int test_malformed_and_corrupted_rejection()
{
  const std::vector<std::uint8_t> payload{0x01U, 0x02U, 0x03U};
  auto wire = flow_control::serialize_frame(
    header(3U, 9U), payload, flow_control::FcsScheme::Crc16
  );
  auto truncated = wire;
  truncated.pop_back();
  wire[flow_control::FRAME_HEADER_SIZE] ^= 0x80U;

  if (flow_control::verify_frame(truncated, flow_control::FcsScheme::Crc16)
        .has_value()
      || flow_control::verify_frame(wire, flow_control::FcsScheme::Crc16)
        .has_value()) {
    std::cerr << "FAIL: malformed/corrupted frame accepted\n";
    return 1;
  }

  std::cout << "PASS: malformed and corrupted frame rejection\n";
  return 0;
}

int test_crc10_left_alignment()
{
  const std::vector<std::uint8_t> payload{0xA5U};
  auto wire = flow_control::serialize_frame(
    header(1U, 3U), payload, flow_control::FcsScheme::Crc10
  );

  if ((wire.back() & 0x3FU) != 0U) {
    std::cerr << "FAIL: CRC-10 is not left-aligned\n";
    return 1;
  }

  wire.back() |= 0x01U;
  if (flow_control::verify_frame(wire, flow_control::FcsScheme::Crc10)
        .has_value()) {
    std::cerr << "FAIL: nonzero CRC-10 padding bits accepted\n";
    return 1;
  }

  std::cout << "PASS: CRC-10 left alignment\n";
  return 0;
}

int test_short_final_payload()
{
  const std::vector<std::uint8_t> payload{0x41U, 0x42U, 0x43U};
  const auto wire = flow_control::serialize_frame(
    header(3U, 4U), payload, flow_control::FcsScheme::Crc32
  );
  const auto parsed = flow_control::verify_frame(
    wire, flow_control::FcsScheme::Crc32
  );

  if (!parsed.has_value() || parsed->payload != payload) {
    std::cerr << "FAIL: short final payload length\n";
    return 1;
  }

  std::cout << "PASS: short final payload length\n";
  return 0;
}

int test_invalid_serialization()
{
  try {
    (void)flow_control::serialize_frame(
      header(2U, 1U), {0x01U}, flow_control::FcsScheme::Checksum16
    );
  } catch (const std::invalid_argument&) {
    try {
      (void)flow_control::serialize_frame(
        header(static_cast<std::uint16_t>(flow_control::MAX_PAYLOAD_SIZE + 1U), 1U),
        std::vector<std::uint8_t>(flow_control::MAX_PAYLOAD_SIZE + 1U),
        flow_control::FcsScheme::Crc32
      );
    } catch (const std::invalid_argument&) {
      std::cout << "PASS: invalid frame serialization rejection\n";
      return 0;
    }
  }

  std::cerr << "FAIL: invalid frame serialization accepted\n";
  return 1;
}

int test_maximum_safe_payload()
{
  const std::vector<std::uint8_t> payload(
    flow_control::MAX_PAYLOAD_SIZE, 0xA5U
  );
  const auto wire = flow_control::serialize_frame(
    header(static_cast<std::uint16_t>(payload.size()), 12U),
    payload,
    flow_control::FcsScheme::Crc32
  );
  const auto parsed = flow_control::verify_frame(
    wire, flow_control::FcsScheme::Crc32
  );

  if (wire.size() != flow_control::MAX_FRAME_SIZE
      || !parsed.has_value()
      || parsed->payload != payload) {
    std::cerr << "FAIL: maximum safe payload boundary\n";
    return 1;
  }

  std::cout << "PASS: maximum safe payload boundary\n";
  return 0;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += test_manual_header_layout();
  failures += test_scheme_padding_sizes();
  failures += test_all_scheme_round_trips();
  failures += test_malformed_and_corrupted_rejection();
  failures += test_crc10_left_alignment();
  failures += test_short_final_payload();
  failures += test_invalid_serialization();
  failures += test_maximum_safe_payload();
  return failures == 0 ? 0 : 1;
}
