/**
 * @file go_back_n.hpp
 * @brief Declares the Go-Back-N ARQ sender and receiver state machines.
 */

#ifndef FLOW_CONTROL_GO_BACK_N_HPP
#define FLOW_CONTROL_GO_BACK_N_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "protocol.hpp"

namespace flow_control
{

/** Largest Go-Back-N sender window that keeps sequence numbers unambiguous. */
inline constexpr std::size_t GO_BACK_N_MAX_WINDOW = 255U;

/**
 * Sender side of Go-Back-N: up to `window_size` outstanding frames, a
 * cumulative ACK slides the window base, and timeout retransmits every
 * outstanding frame from the base.
 */
class GoBackNSender
{
public:
  /**
   * @param frame_count Total number of frames to send.
   * @param window_size Maximum outstanding frames, 1--255.
   * @param start_sequence Sequence number of the first frame.
   * @throws std::invalid_argument if window_size is 0 or exceeds 255.
   */
  GoBackNSender(
    std::size_t frame_count,
    std::size_t window_size,
    std::uint8_t start_sequence = 0U
  );

  /** Returns pending retransmissions (on timeout) plus any new sends. */
  std::vector<Transmission> transmissions();

  /**
   * @brief Processes a cumulative ACK, sliding the window base forward.
   * @return True if the sequence matched an outstanding frame.
   */
  bool acknowledge(std::uint8_t sequence);

  /** Marks every outstanding frame for retransmission from the base. */
  void timeout();

  /** True once every frame has been acknowledged. */
  bool complete() const noexcept;

private:
  std::size_t frame_count_;
  std::size_t window_size_;
  std::uint8_t start_sequence_;
  std::size_t base_;
  std::size_t next_to_send_;
  bool retransmit_pending_;

  std::uint8_t sequence_of(std::size_t frame_index) const noexcept;
};

/**
 * Receiver side of Go-Back-N: accepts only the next expected sequence and
 * discards out-of-order frames, re-acknowledging the last accepted one.
 */
class GoBackNReceiver
{
public:
  explicit GoBackNReceiver(std::uint8_t start_sequence = 0U);

  /**
   * @brief Processes one verified, arriving frame.
   * @param frame_index Index of the frame within the full message.
   * @param sequence Sequence number carried on the wire.
   */
  ReceiveResult receive(std::size_t frame_index, std::uint8_t sequence);

private:
  std::uint8_t expected_sequence_;
  std::uint8_t last_acked_sequence_;
  bool has_received_;
};

}  // namespace flow_control

#endif
