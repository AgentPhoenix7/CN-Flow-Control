#include "frame.hpp"

#include "checksum.hpp"
#include "crc.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace flow_control
{

namespace
{

const CrcParameters& crc_parameters(FcsScheme scheme)
{
  switch (scheme) {
    case FcsScheme::Crc8:
      return CRC8_PARAMETERS;
    case FcsScheme::Crc10:
      return CRC10_PARAMETERS;
    case FcsScheme::Crc16:
      return CRC16_PARAMETERS;
    case FcsScheme::Crc32:
      return CRC32_PARAMETERS;
    case FcsScheme::Checksum16:
      break;
  }

  throw std::invalid_argument("checksum scheme has no CRC parameters");
}

void append_big_endian(
  std::vector<std::uint8_t>& bytes,
  std::uint32_t value,
  std::size_t width
)
{
  for (std::size_t index = width; index > 0U; --index) {
    const std::size_t shift = (index - 1U) * 8U;
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

std::uint32_t read_big_endian(
  const std::vector<std::uint8_t>& bytes,
  std::size_t offset,
  std::size_t width
)
{
  std::uint32_t value = 0U;

  for (std::size_t index = 0U; index < width; ++index) {
    value = (value << 8U) | bytes[offset + index];
  }

  return value;
}

void append_fcs(
  std::vector<std::uint8_t>& wire,
  const std::vector<std::uint8_t>& protected_bytes,
  FcsScheme scheme
)
{
  if (scheme == FcsScheme::Checksum16) {
    append_big_endian(wire, checksum16_compute(protected_bytes), 2U);
    return;
  }

  std::uint32_t remainder = crc_compute(
    protected_bytes, crc_parameters(scheme)
  );

  if (scheme == FcsScheme::Crc10) {
    remainder <<= 6U;
  }

  append_big_endian(wire, remainder, fcs_size(scheme));
}

bool fcs_matches(
  const std::vector<std::uint8_t>& wire,
  const std::vector<std::uint8_t>& protected_bytes,
  FcsScheme scheme
)
{
  const std::size_t size = fcs_size(scheme);
  std::uint32_t received = read_big_endian(
    wire, wire.size() - size, size
  );

  if (scheme == FcsScheme::Checksum16) {
    return checksum16_verify(
      protected_bytes, static_cast<std::uint16_t>(received)
    );
  }

  if (scheme == FcsScheme::Crc10) {
    if ((received & 0x3FU) != 0U) {
      return false;
    }
    received >>= 6U;
  }

  return crc_verify(protected_bytes, received, crc_parameters(scheme));
}

}  // namespace

std::size_t fcs_size(FcsScheme scheme)
{
  switch (scheme) {
    case FcsScheme::Checksum16:
      return CHECKSUM16_SIZE;
    case FcsScheme::Crc8:
      return CRC8_SIZE;
    case FcsScheme::Crc10:
      return CRC10_SIZE;
    case FcsScheme::Crc16:
      return CRC16_SIZE;
    case FcsScheme::Crc32:
      return CRC32_SIZE;
  }

  throw std::invalid_argument("unsupported FCS scheme");
}

std::vector<std::uint8_t> serialize_frame(
  const FrameHeader& header,
  const std::vector<std::uint8_t>& payload,
  FcsScheme scheme
)
{
  if (payload.size() != header.valid_payload_length) {
    throw std::invalid_argument("header payload length does not match data");
  }

  if (payload.size() > MAX_PAYLOAD_SIZE) {
    throw std::invalid_argument("payload exceeds the safe frame limit");
  }

  const std::size_t fcs_bytes = fcs_size(scheme);
  const std::size_t unpadded_size =
    FRAME_HEADER_SIZE + payload.size() + fcs_bytes;
  const std::size_t wire_size = std::max(MIN_FRAME_SIZE, unpadded_size);
  std::vector<std::uint8_t> protected_bytes;
  protected_bytes.reserve(wire_size - fcs_bytes);
  protected_bytes.insert(
    protected_bytes.end(), header.source.begin(), header.source.end()
  );
  protected_bytes.insert(
    protected_bytes.end(), header.destination.begin(), header.destination.end()
  );
  protected_bytes.push_back(
    static_cast<std::uint8_t>(header.valid_payload_length >> 8U)
  );
  protected_bytes.push_back(
    static_cast<std::uint8_t>(header.valid_payload_length)
  );
  protected_bytes.push_back(header.sequence);
  protected_bytes.insert(
    protected_bytes.end(), payload.begin(), payload.end()
  );
  protected_bytes.resize(wire_size - fcs_bytes, 0U);

  std::vector<std::uint8_t> wire = protected_bytes;
  append_fcs(wire, protected_bytes, scheme);
  return wire;
}

std::optional<VerifiedFrame> verify_frame(
  const std::vector<std::uint8_t>& wire,
  FcsScheme scheme
)
{
  const std::size_t fcs_bytes = fcs_size(scheme);

  if (wire.size() < MIN_FRAME_SIZE
      || wire.size() > MAX_FRAME_SIZE
      || wire.size() < FRAME_HEADER_SIZE + fcs_bytes) {
    return std::nullopt;
  }

  const std::uint16_t valid_length = static_cast<std::uint16_t>(
    (static_cast<std::uint16_t>(wire[12U]) << 8U) | wire[13U]
  );
  const std::size_t expected_size = std::max(
    MIN_FRAME_SIZE,
    FRAME_HEADER_SIZE + static_cast<std::size_t>(valid_length) + fcs_bytes
  );

  if (valid_length > MAX_PAYLOAD_SIZE || wire.size() != expected_size) {
    return std::nullopt;
  }

  const std::size_t payload_end =
    FRAME_HEADER_SIZE + static_cast<std::size_t>(valid_length);
  const std::size_t protected_end = wire.size() - fcs_bytes;

  if (!std::all_of(
        wire.begin() + static_cast<std::ptrdiff_t>(payload_end),
        wire.begin() + static_cast<std::ptrdiff_t>(protected_end),
        [](std::uint8_t byte) { return byte == 0U; }
      )) {
    return std::nullopt;
  }

  const std::vector<std::uint8_t> protected_bytes(
    wire.begin(),
    wire.begin() + static_cast<std::ptrdiff_t>(protected_end)
  );

  if (!fcs_matches(wire, protected_bytes, scheme)) {
    return std::nullopt;
  }

  FrameHeader header{};
  std::copy_n(wire.begin(), MAC_ADDRESS_SIZE, header.source.begin());
  std::copy_n(
    wire.begin() + static_cast<std::ptrdiff_t>(MAC_ADDRESS_SIZE),
    MAC_ADDRESS_SIZE,
    header.destination.begin()
  );
  header.valid_payload_length = valid_length;
  header.sequence = wire[14U];

  return VerifiedFrame{
    header,
    std::vector<std::uint8_t>(
      wire.begin() + static_cast<std::ptrdiff_t>(FRAME_HEADER_SIZE),
      wire.begin() + static_cast<std::ptrdiff_t>(payload_end)
    )
  };
}

}  // namespace flow_control
