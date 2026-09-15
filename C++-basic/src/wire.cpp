#include "wire.hpp"

#include <stdexcept>

#include "frame.hpp"

namespace basic_flow {

namespace {

std::size_t body_length_for_tag(std::uint8_t tag) {
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

void send_message(const TcpSocket& socket, std::uint8_t tag, const std::vector<std::uint8_t>& body) {
  std::vector<std::uint8_t> out;
  out.reserve(1 + body.size());
  out.push_back(tag);
  out.insert(out.end(), body.begin(), body.end());
  socket.send_exact(out);
}

RecvMessage try_recv_message(const TcpSocket& socket, int timeout_ms) {
  socket.set_recv_timeout_ms(timeout_ms);
  RecvMessage msg;
  msg.status = socket.recv_timed_byte(msg.tag);
  if (msg.status != RecvStatus::Ok) return msg;

  // The tag has arrived, so the peer's single send_exact() write for this message is
  // already in flight on this TCP connection; block for the rest instead of racing
  // the same short timeout against it.
  socket.set_recv_timeout_ms(0);
  msg.body = socket.recv_exact_blocking(body_length_for_tag(msg.tag));
  return msg;
}

std::vector<std::uint8_t> make_ack_body(std::uint8_t seq) {
  return {seq, seq};
}

std::optional<std::uint8_t> parse_ack_body(const std::vector<std::uint8_t>& body) {
  if (body.size() != 2 || body[0] != body[1]) return std::nullopt;
  return body[0];
}

}  // namespace basic_flow

/*
GET BODY LENGTH:

if tag == DATA:
  return frame body length

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