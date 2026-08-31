/**
 * @file selective_repeat.hpp
 * @brief Declares the Selective Repeat ARQ sender and receiver state
 * machines.
 */

#ifndef FLOW_CONTROL_SELECTIVE_REPEAT_HPP
#define FLOW_CONTROL_SELECTIVE_REPEAT_HPP

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "protocol.hpp"

namespace flow_control
{

/** Largest Selective Repeat window that keeps sequence numbers unambiguous. */
inline constexpr std::size_t SELECTIVE_REPEAT_MAX_WINDOW = 128U;

/**
 * Sender side of Selective Repeat: up to `window_size` outstanding frames
 * with independent ACKs; timeout retransmits only the frames still
 * unacknowledged.
 */
class SelectiveRepeatSender
{
public:
  /**
   * @param frame_count Total number of frames to send.
   * @param window_size Maximum outstanding frames, 1--128.
   * @param start_sequence Sequence number of the first frame.
   * @throws std::invalid_argument if window_size is 0 or exceeds 128.
   */
  SelectiveRepeatSender(
    std::size_t frame_count,
    std::size_t window_size,
    std::uint8_t start_sequence = 0U
  );

  /**
   * @brief Returns pending retransmissions (on timeout) plus any new sends.
   * Each retransmission is reported at most once per `timeout()` call and
   * each new send at most once, so repeated calls without an intervening
   * `timeout()` or freed window slot return an empty vector.
   */
  std::vector<Transmission> transmissions();

  /**
   * @brief Processes an independent ACK for one frame.
   * @return True if the sequence matched an outstanding, unacknowledged
   * frame.
   */
  bool acknowledge(std::uint8_t sequence);

  /** Marks every currently outstanding, unacknowledged frame for resend. */
  void timeout();

  /** True once every frame has been acknowledged. */
  bool complete() const noexcept;

private:
  std::size_t frame_count_;
  std::size_t window_size_;
  std::uint8_t start_sequence_;
  std::size_t base_;
  std::size_t next_to_send_;
  std::vector<bool> acked_;
  std::vector<bool> retransmit_pending_;

  std::uint8_t sequence_of(std::size_t frame_index) const noexcept;
};

/**
 * Receiver side of Selective Repeat: accepts and independently
 * acknowledges any frame within the receive window, buffering
 * out-of-order frames and delivering only contiguous data.
 *
 * This state machine tracks position purely by `frame_index`, not by
 * reconstructing expected sequence numbers, so it does not independently
 * validate the wire `sequence` passed to `receive()` -- it trusts the
 * caller's already-verified frame and echoes that sequence back in the ACK.
 */
class SelectiveRepeatReceiver
{
public:
  /**
   * @param window_size Maximum frames the receiver may buffer ahead, 1--128.
   * @throws std::invalid_argument if window_size is 0 or exceeds 128.
   */
  explicit SelectiveRepeatReceiver(std::size_t window_size);

  /**
   * @brief Processes one verified, arriving frame.
   * @param frame_index Index of the frame within the full message.
   * @param sequence Sequence number carried on the wire.
   */
  ReceiveResult receive(std::size_t frame_index, std::uint8_t sequence);

private:
  std::size_t window_size_;
  std::size_t next_deliver_index_;
  std::set<std::size_t> buffered_indices_;
};

}  // namespace flow_control

#endif
