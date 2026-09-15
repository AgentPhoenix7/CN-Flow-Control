#include "frame.hpp"

#include <algorithm>

#include "crc16.hpp"

namespace basic_flow {

std::vector<std::uint8_t> serialize(const DataFrame& frame) {
  std::vector<std::uint8_t> body;
  body.reserve(FRAME_BODY_LEN);

  for (auto b : frame.src_mac) body.push_back(b);
  for (auto b : frame.dst_mac) body.push_back(b);
  body.push_back(static_cast<std::uint8_t>(frame.length >> 8));
  body.push_back(static_cast<std::uint8_t>(frame.length & 0xFF));
  body.push_back(frame.seq);
  for (auto b : frame.payload) body.push_back(b);

  std::uint16_t crc = crc16_compute(body);
  body.push_back(static_cast<std::uint8_t>(crc >> 8));
  body.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  return body;
}

bool deserialize_and_verify(const std::vector<std::uint8_t>& body, DataFrame& out) {
  if (body.size() != FRAME_BODY_LEN) return false;

  std::vector<std::uint8_t> covered(body.begin(), body.end() - static_cast<long>(CRC_LEN));
  std::uint16_t received_crc = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(body[FRAME_BODY_LEN - 2]) << 8) | body[FRAME_BODY_LEN - 1]);
  if (crc16_compute(covered) != received_crc) return false;

  std::size_t pos = 0;
  for (auto& b : out.src_mac) b = body[pos++];
  for (auto& b : out.dst_mac) b = body[pos++];
  out.length = static_cast<std::uint16_t>((static_cast<std::uint16_t>(body[pos]) << 8) | body[pos + 1]);
  pos += 2;
  out.seq = body[pos++];
  for (auto& b : out.payload) b = body[pos++];

  if (out.length > PAYLOAD_LEN) return false;  // malformed length field
  return true;
}

std::vector<DataFrame> split_into_frames(const std::vector<std::uint8_t>& file_bytes) {
  std::vector<DataFrame> frames;
  std::size_t offset = 0;
  std::uint8_t seq = 0;

  while (offset < file_bytes.size()) {
    DataFrame frame;
    frame.src_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    frame.dst_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
    frame.seq = seq;
    std::size_t take = std::min(PAYLOAD_LEN, file_bytes.size() - offset);
    frame.length = static_cast<std::uint16_t>(take);
    std::copy(file_bytes.begin() + static_cast<long>(offset),
              file_bytes.begin() + static_cast<long>(offset + take), frame.payload.begin());
    frames.push_back(frame);
    offset += take;
    seq = static_cast<std::uint8_t>(seq + 1);
  }
  return frames;
}

}  // namespace basic_flow

/*
SERIALIZE FRAME:

create empty body

append source MAC
append destination MAC

append upper byte of length
append lower byte of length

append sequence number
append payload

crc = calculate CRC of body

append upper byte of crc
append lower byte of crc

return body


DESERIALIZE AND VERIFY FRAME:

if body size is not correct:
  return false

take all bytes except last 2 as CRC-covered data

received_crc =
  combine second-last byte and last byte

calculated_crc = calculate CRC of covered data

if calculated_crc != received_crc:
  return false

read source MAC
read destination MAC
read length
read sequence number
read payload

if length > maximum payload length:
  return false

return true


SPLIT FILE INTO FRAMES:

frames = empty list
offset = 0
sequence = 0

while offset < file size:
  create new frame

  set source MAC
  set destination MAC
  set sequence number

  take = minimum(max payload size, remaining file size)

  frame.length = take

  copy "take" bytes from file into payload

  add frame to frames

  offset = offset + take
  sequence = sequence + 1

return frames
*/