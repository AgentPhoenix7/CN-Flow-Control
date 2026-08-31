/**
 * @file protocol.hpp
 * @brief Shared value types for the ARQ sender/receiver state machines.
 */

#ifndef FLOW_CONTROL_PROTOCOL_HPP
#define FLOW_CONTROL_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace flow_control
{

/** One frame that an ARQ sender currently believes should be transmitted. */
struct Transmission
{
  /** Index of the frame within the full message. */
  std::size_t frame_index;
  /** Modulo-256 sequence number carried on the wire. */
  std::uint8_t sequence;
  /** True when this transmission is a retransmission, not a new send. */
  bool retransmission;
};

/** Outcome of an ARQ receiver processing one arriving, verified frame. */
struct ReceiveResult
{
  /** Sequence number to acknowledge, if the frame warrants an ACK. */
  std::optional<std::uint8_t> ack;
  /** Frame indices newly delivered to the application, in order. */
  std::vector<std::size_t> delivered_indices;
  /** True when the frame repeats one already delivered or acknowledged. */
  bool duplicate = false;
  /** True when the frame could not be accepted in sequence order. */
  bool out_of_order = false;
};

}  // namespace flow_control

#endif
