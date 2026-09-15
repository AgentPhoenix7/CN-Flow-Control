#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "socket.hpp"

namespace basic_flow {

enum : std::uint8_t {
  TAG_DATA = 0x01,
  TAG_ACK = 0x02,
  TAG_DONE = 0x03,      ///< sender -> receiver: no more frames coming
  TAG_DONE_ACK = 0x04,  ///< receiver -> sender: DONE received, safe to exit
};

/// TCP is only the carrier here: this 1-byte tag plus each tag's fixed body length
/// is the *external* record framing, kept outside the simulated data frame so a
/// corrupted simulated header can never desynchronize the byte stream.
struct RecvMessage {
  RecvStatus status = RecvStatus::Closed;
  std::uint8_t tag = 0;
  std::vector<std::uint8_t> body;
};

/// Sends one tagged message (tag byte followed by its body, if any).
void send_message(const TcpSocket& socket, std::uint8_t tag, const std::vector<std::uint8_t>& body = {});

/// Waits up to timeout_ms (0 = forever) for the next tagged message and reads its
/// whole fixed-size body. Only Ok carries a usable tag/body; Timeout/Closed carry
/// neither -- callers must check status before touching tag or body.
RecvMessage try_recv_message(const TcpSocket& socket, int timeout_ms);

/// 2-byte ACK body: the sequence number repeated. Simple stand-in for a full CRC on
/// the ACK path -- lets the sender detect a single-bit corruption of the ACK itself.
std::vector<std::uint8_t> make_ack_body(std::uint8_t seq);

/// Returns the sequence number if both copies still agree, or nullopt if the ACK
/// body was corrupted in transit (or is the wrong size).
std::optional<std::uint8_t> parse_ack_body(const std::vector<std::uint8_t>& body);

}  // namespace basic_flow
