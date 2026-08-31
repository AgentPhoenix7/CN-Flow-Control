/**
 * @file record.hpp
 * @brief Declares typed application records exchanged over the TCP carrier.
 */

#ifndef FLOW_CONTROL_RECORD_HPP
#define FLOW_CONTROL_RECORD_HPP

#include "frame.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace flow_control
{

/** Internal record tag; the external TCP length prefix is not included. */
enum class RecordType : std::uint8_t
{
  Config = 1U,
  Data = 2U,
  Ack = 3U,
  RoundEnd = 4U,
  AckEnd = 5U,
  Complete = 6U,
  CompleteAck = 7U
};

/** Supported application-layer ARQ protocols. */
enum class ArqProtocol : std::uint8_t
{
  StopAndWait = 1U,
  GoBackN = 2U,
  SelectiveRepeat = 3U
};

/** Validated transfer parameters carried by a CONFIG record. */
struct SessionConfig
{
  ArqProtocol protocol;
  FcsScheme fcs;
  std::uint16_t window_size;
  std::uint16_t payload_size;
  std::uint16_t data_error_permyriad;
  std::uint16_t data_delay_permyriad;
  std::uint16_t ack_error_permyriad;
  std::uint16_t ack_delay_permyriad;
  std::uint32_t selection_seed;
  std::uint32_t bit_seed;
};

/** A parsed internal application record. */
struct Record
{
  RecordType type;
  std::vector<std::uint8_t> body;
};

/** @brief Serializes a validated record as tag followed by body. */
std::vector<std::uint8_t> serialize_record(const Record& record);

/** @brief Parses and validates one complete internal record. */
std::optional<Record> parse_record(
  const std::vector<std::uint8_t>& bytes
);

/** @brief Builds an ACK containing sequence and bitwise complement. */
Record make_ack_record(std::uint8_t sequence);

/** @brief Extracts a valid ACK sequence or returns std::nullopt. */
std::optional<std::uint8_t> ack_sequence(const Record& record);

/** @brief Validates and encodes a fixed-width CONFIG record. */
Record make_config_record(const SessionConfig& config);

/** @brief Decodes a valid CONFIG record or returns std::nullopt. */
std::optional<SessionConfig> session_config(const Record& record);

}  // namespace flow_control

#endif
