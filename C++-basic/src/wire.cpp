#include "wire.hpp"

#include <stdexcept>

#include "frame.hpp"

namespace basic_flow {

namespace {

// Fixed body sizes exclude the external tag: DATA=65 (with CRC-32), ACK=2,
// DONE/DONE_ACK=0. Unknown tags cannot be decoded and are rejected.
std::size_t body_length_for_tag(const std::uint8_t tag) {
  switch (tag) {
    case TAG_DATA:
      return FRAME_BODY_LEN;
    case TAG_ACK:
      return 2;
    case TAG_DONE:
    case TAG_DONE_ACK:
      return 0;
    default:
      throw std::runtime_error("unknown wire tag");
  }
}

}  // namespace

// Sends one tagged message (tag byte followed by its body, if any).
void send_message(const TcpSocket& socket, const std::uint8_t tag, const std::vector<std::uint8_t>& body) {
  // The tag is always sent, even if the body is empty.
  // DATA is 1 tag + 65 frame bytes = 66 bytes; the tag is outside CRC protection.
  // The body length is fixed for each tag, so the receiver knows exactly how many
  // bytes to read after the tag. This is a simple framing protocol on top of TCP.
  std::vector<std::uint8_t> out;
  out.reserve(1 + body.size());
  out.push_back(tag);
  out.insert(out.end(), body.begin(), body.end());
  socket.send_exact(out);
}

// Waits up to timeout_ms (0 = forever) for the next tagged message and reads its
// whole fixed-size body. Only Ok carries a usable tag/body; Timeout/Closed carry
// neither -- callers must check status before touching tag or body.
RecvMessage try_recv_message(const TcpSocket& socket, const int timeout_ms) {
  socket.set_recv_timeout_ms(timeout_ms);
  RecvMessage msg;
  msg.status = socket.recv_timed_byte(msg.tag);
  if (msg.status != RecvStatus::Ok) return msg;

  // Once a tag arrives, finish its fixed-length body with a blocking read so a
  // timeout cannot leave the next receive starting in the middle of this message.
  socket.set_recv_timeout_ms(0);
  // Determine the expected body length from the received tag and read exactly
  // that many bytes.
  msg.body = socket.recv_exact_blocking(body_length_for_tag(msg.tag));
  return msg;
}

// 2-byte ACK body: the sequence number repeated. Simple stand-in for a full CRC on
// the ACK path -- lets the sender detect a single-bit corruption of the ACK itself.
std::vector<std::uint8_t> make_ack_body(std::uint8_t seq) {
  return {seq, seq};
}

// Returns the sequence number if both copies still agree, or nullopt if the ACK
// body was corrupted in transit (or is the wrong size).
std::optional<std::uint8_t> parse_ack_body(const std::vector<std::uint8_t>& body) {
  if (body.size() != 2 || body[0] != body[1]) return std::nullopt;
  return body[0];
}

}  // namespace basic_flow

/*
GET BODY LENGTH:

if tag == DATA:
  return 65 (15-byte header + 46-byte payload + four-byte CRC-32 FCS)

if tag == ACK:
  return 2

if tag == DONE:
  return 0

if tag == DONE_ACK:
  return 0

otherwise:
  report unknown tag
*/

/*
SEND MESSAGE:

create empty output

append message tag
append message body

DATA is 66 bytes including tag; ACK is 3; DONE and DONE_ACK are 1 each

send all bytes through TCP socket
*/

/*
RECEIVE MESSAGE:

set socket receive timeout

try to receive one byte as message tag

if timeout occurs:
  return Timeout

if connection is closed:
  return Closed

determine expected body length from received tag

disable receive timeout

receive exactly that many body bytes

return complete message
*/

/*
CREATE ACK:

return:
  [sequence, sequence]
*/

/*
PARSE ACK:

if ACK body size != 2:
  return invalid

if first byte != second byte:
  return invalid

return first byte as sequence number
*/
