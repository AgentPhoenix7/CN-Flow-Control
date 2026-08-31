#include "config.hpp"
#include "record.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{

int test_record_round_trips()
{
  const std::vector<flow_control::Record> records{
    {flow_control::RecordType::Data,
     std::vector<std::uint8_t>(flow_control::MIN_FRAME_SIZE, 0xA5U)},
    flow_control::make_ack_record(0x5AU),
    {flow_control::RecordType::RoundEnd, {}},
    {flow_control::RecordType::AckEnd, {}},
    {flow_control::RecordType::Complete, {}},
    {flow_control::RecordType::CompleteAck, {}}
  };

  for (const auto& record : records) {
    const auto parsed = flow_control::parse_record(
      flow_control::serialize_record(record)
    );
    if (!parsed.has_value()
        || parsed->type != record.type
        || parsed->body != record.body) {
      std::cerr << "FAIL: typed record round trip\n";
      return 1;
    }
  }

  std::cout << "PASS: typed record round trips\n";
  return 0;
}

int test_ack_integrity()
{
  const auto ack = flow_control::make_ack_record(0xA5U);
  const auto sequence = flow_control::ack_sequence(ack);
  flow_control::Record corrupted = ack;
  corrupted.body[1] ^= 0x01U;

  if (!sequence.has_value()
      || *sequence != 0xA5U
      || flow_control::ack_sequence(corrupted).has_value()
      || flow_control::parse_record(
        {static_cast<std::uint8_t>(flow_control::RecordType::Ack),
         0xA5U, 0x5BU}
      ).has_value()) {
    std::cerr << "FAIL: ACK complement integrity\n";
    return 1;
  }

  std::cout << "PASS: ACK complement integrity\n";
  return 0;
}

int test_config_round_trip()
{
  const flow_control::SessionConfig config{
    flow_control::ArqProtocol::SelectiveRepeat,
    flow_control::FcsScheme::Crc32,
    64U,
    46U,
    1000U,
    2000U,
    3000U,
    4000U,
    0x01020304U,
    0xA0B0C0D0U
  };
  const auto record = flow_control::make_config_record(config);
  const auto parsed_record = flow_control::parse_record(
    flow_control::serialize_record(record)
  );

  if (!parsed_record.has_value()) {
    std::cerr << "FAIL: CONFIG record parse\n";
    return 1;
  }

  const auto parsed = flow_control::session_config(*parsed_record);
  if (!parsed.has_value()
      || parsed->protocol != config.protocol
      || parsed->fcs != config.fcs
      || parsed->window_size != config.window_size
      || parsed->payload_size != config.payload_size
      || parsed->data_error_permyriad != config.data_error_permyriad
      || parsed->data_delay_permyriad != config.data_delay_permyriad
      || parsed->ack_error_permyriad != config.ack_error_permyriad
      || parsed->ack_delay_permyriad != config.ack_delay_permyriad
      || parsed->selection_seed != config.selection_seed
      || parsed->bit_seed != config.bit_seed) {
    std::cerr << "FAIL: CONFIG round trip\n";
    return 1;
  }

  std::cout << "PASS: CONFIG round trip\n";
  return 0;
}

int test_malformed_record_rejection()
{
  const std::vector<std::vector<std::uint8_t>> malformed{
    {},
    {0xFFU},
    {static_cast<std::uint8_t>(flow_control::RecordType::Data), 0x00U},
    {static_cast<std::uint8_t>(flow_control::RecordType::Ack), 0x01U},
    {static_cast<std::uint8_t>(flow_control::RecordType::RoundEnd), 0x00U},
    {static_cast<std::uint8_t>(flow_control::RecordType::Config), 0x00U}
  };

  for (const auto& bytes : malformed) {
    if (flow_control::parse_record(bytes).has_value()) {
      std::cerr << "FAIL: malformed record accepted\n";
      return 1;
    }
  }

  try {
    flow_control::SessionConfig invalid{
      flow_control::ArqProtocol::SelectiveRepeat,
      flow_control::FcsScheme::Crc8,
      129U,
      46U,
      0U, 0U, 0U, 0U,
      1U, 2U
    };
    (void)flow_control::make_config_record(invalid);
  } catch (const std::invalid_argument&) {
    std::cout << "PASS: malformed record rejection\n";
    return 0;
  }

  std::cerr << "FAIL: invalid CONFIG accepted\n";
  return 1;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += test_record_round_trips();
  failures += test_ack_integrity();
  failures += test_config_round_trip();
  failures += test_malformed_record_rejection();
  return failures == 0 ? 0 : 1;
}
