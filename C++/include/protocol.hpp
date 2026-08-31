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

/**
 * Outcome of an ARQ receiver processing one arriving, verified frame.
 *
 * `duplicate` and `out_of_order` are independent flags with one meaning
 * shared by all three protocols: `duplicate` is true when this frame's
 * payload was already delivered or already buffered and must not be
 * written again; `out_of_order` is true when this frame was not the one
 * currently expected for in-order delivery when it arrived. A repeat of
 * the immediately preceding, already-delivered frame is therefore
 * `duplicate=true, out_of_order=false` (it *was* the expected frame when
 * first delivered), while a frame arriving ahead of a gap -- whether or
 * not it has been seen before -- is `out_of_order=true`.
 */
struct ReceiveResult
{
  /** Sequence number to acknowledge, if the frame warrants an ACK. */
  std::optional<std::uint8_t> ack;
  /** Frame indices newly delivered to the application, in order. */
  std::vector<std::size_t> delivered_indices;
  /** True when the frame's payload was already delivered or buffered. */
  bool duplicate = false;
  /** True when the frame was not the one currently expected for delivery. */
  bool out_of_order = false;
};

}  // namespace flow_control

#endif
