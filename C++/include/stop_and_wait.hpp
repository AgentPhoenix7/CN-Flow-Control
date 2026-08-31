/**
 * @file stop_and_wait.hpp
 * @brief Declares the Stop-and-Wait ARQ sender and receiver state machines.
 */

#ifndef FLOW_CONTROL_STOP_AND_WAIT_HPP
#define FLOW_CONTROL_STOP_AND_WAIT_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "protocol.hpp"

namespace flow_control
{

/**
 * Sender side of Stop-and-Wait: exactly one outstanding frame at a time.
 * The next frame is sent only after its matching ACK; timeout retransmits
 * the same outstanding frame.
 */
class StopAndWaitSender
{
public:
  /**
   * @param frame_count Total number of frames to send.
   * @param start_sequence Sequence number of the first frame.
   */
  explicit StopAndWaitSender(
    std::size_t frame_count,
    std::uint8_t start_sequence = 0U
  );

  /** Returns the currently outstanding new send or retransmission, if any. */
  std::vector<Transmission> transmissions();

  /**
   * @brief Processes an ACK.
   * @return True if it matched the outstanding frame's sequence.
   */
  bool acknowledge(std::uint8_t sequence);

  /** Marks the outstanding frame for retransmission. */
  void timeout();

  /** True once every frame has been acknowledged. */
  bool complete() const noexcept;

private:
  std::size_t frame_count_;
  std::uint8_t start_sequence_;
  std::size_t next_index_;
  bool has_outstanding_;
  bool retransmit_pending_;
};

/**
 * Receiver side of Stop-and-Wait: accepts only the expected sequence and
 * re-acknowledges the immediately preceding one on duplicate delivery.
 */
class StopAndWaitReceiver
{
public:
  StopAndWaitReceiver();

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
