#include "record.hpp"

#include "config.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace flow_control
{

namespace
{

constexpr std::size_t CONFIG_BODY_SIZE = 22U;
constexpr std::uint16_t PROBABILITY_SCALE = 10000U;

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

std::uint16_t read_u16(
  const std::vector<std::uint8_t>& bytes,
  std::size_t offset
)
{
  return static_cast<std::uint16_t>(
    (static_cast<std::uint16_t>(bytes[offset]) << 8U)
    | bytes[offset + 1U]
  );
}

std::uint32_t read_u32(
  const std::vector<std::uint8_t>& bytes,
  std::size_t offset
)
{
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U)
    | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U)
    | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U)
    | bytes[offset + 3U];
}

bool valid_fcs(FcsScheme scheme)
{
  switch (scheme) {
    case FcsScheme::Checksum16:
    case FcsScheme::Crc8:
    case FcsScheme::Crc10:
    case FcsScheme::Crc16:
    case FcsScheme::Crc32:
      return true;
  }

  return false;
}

bool valid_config(const SessionConfig& config)
{
  if (!valid_fcs(config.fcs)
      || config.payload_size == 0U
      || config.payload_size > MAX_PAYLOAD_SIZE
      || config.data_error_permyriad > PROBABILITY_SCALE
      || config.data_delay_permyriad > PROBABILITY_SCALE
      || config.ack_error_permyriad > PROBABILITY_SCALE
      || config.ack_delay_permyriad > PROBABILITY_SCALE) {
    return false;
  }

  switch (config.protocol) {
    case ArqProtocol::StopAndWait:
      return config.window_size == 1U;
    case ArqProtocol::GoBackN:
      return config.window_size >= 1U && config.window_size <= 255U;
    case ArqProtocol::SelectiveRepeat:
      return config.window_size >= 1U && config.window_size <= 128U;
  }

  return false;
}

std::optional<SessionConfig> decode_config_body(
  const std::vector<std::uint8_t>& body
)
{
  if (body.size() != CONFIG_BODY_SIZE) {
    return std::nullopt;
  }

  const SessionConfig config{
    static_cast<ArqProtocol>(body[0U]),
    static_cast<FcsScheme>(body[1U]),
    read_u16(body, 2U),
    read_u16(body, 4U),
    read_u16(body, 6U),
    read_u16(body, 8U),
    read_u16(body, 10U),
    read_u16(body, 12U),
    read_u32(body, 14U),
    read_u32(body, 18U)
  };

  if (!valid_config(config)) {
    return std::nullopt;
  }

  return config;
}

std::optional<RecordType> decode_type(std::uint8_t value)
{
  switch (static_cast<RecordType>(value)) {
    case RecordType::Config:
    case RecordType::Data:
    case RecordType::Ack:
    case RecordType::RoundEnd:
    case RecordType::AckEnd:
    case RecordType::Complete:
    case RecordType::CompleteAck:
      return static_cast<RecordType>(value);
  }

  return std::nullopt;
}

bool valid_record(const Record& record)
{
  switch (record.type) {
    case RecordType::Config:
      return decode_config_body(record.body).has_value();
    case RecordType::Data:
      return record.body.size() >= MIN_FRAME_SIZE
        && record.body.size() <= MAX_FRAME_SIZE;
    case RecordType::Ack:
      return record.body.size() == 2U
        && record.body[1U]
          == static_cast<std::uint8_t>(~record.body[0U]);
    case RecordType::RoundEnd:
    case RecordType::AckEnd:
    case RecordType::Complete:
    case RecordType::CompleteAck:
      return record.body.empty();
  }

  return false;
}

}  // namespace

std::vector<std::uint8_t> serialize_record(const Record& record)
{
  if (!valid_record(record)) {
    throw std::invalid_argument("record body does not match its type");
  }

  std::vector<std::uint8_t> bytes;
  bytes.reserve(record.body.size() + 1U);
  bytes.push_back(static_cast<std::uint8_t>(record.type));
  bytes.insert(bytes.end(), record.body.begin(), record.body.end());
  return bytes;
}

std::optional<Record> parse_record(
  const std::vector<std::uint8_t>& bytes
)
{
  if (bytes.empty()) {
    return std::nullopt;
  }

  const auto type = decode_type(bytes[0U]);
  if (!type.has_value()) {
    return std::nullopt;
  }

  Record record{
    *type,
    std::vector<std::uint8_t>(bytes.begin() + 1, bytes.end())
  };

  if (!valid_record(record)) {
    return std::nullopt;
  }

  return record;
}

Record make_ack_record(std::uint8_t sequence)
{
  return {
    RecordType::Ack,
    {sequence, static_cast<std::uint8_t>(~sequence)}
  };
}

std::optional<std::uint8_t> ack_sequence(const Record& record)
{
  if (record.type != RecordType::Ack || !valid_record(record)) {
    return std::nullopt;
  }

  return record.body[0U];
}

Record make_config_record(const SessionConfig& config)
{
  if (!valid_config(config)) {
    throw std::invalid_argument("invalid session configuration");
  }

  std::vector<std::uint8_t> body;
  body.reserve(CONFIG_BODY_SIZE);
  body.push_back(static_cast<std::uint8_t>(config.protocol));
  body.push_back(static_cast<std::uint8_t>(config.fcs));
  append_u16(body, config.window_size);
  append_u16(body, config.payload_size);
  append_u16(body, config.data_error_permyriad);
  append_u16(body, config.data_delay_permyriad);
  append_u16(body, config.ack_error_permyriad);
  append_u16(body, config.ack_delay_permyriad);
  append_u32(body, config.selection_seed);
  append_u32(body, config.bit_seed);
  return {RecordType::Config, std::move(body)};
}

std::optional<SessionConfig> session_config(const Record& record)
{
  if (record.type != RecordType::Config) {
    return std::nullopt;
  }

  return decode_config_body(record.body);
}

}  // namespace flow_control
