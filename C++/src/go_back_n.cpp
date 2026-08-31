#include "go_back_n.hpp"

#include <stdexcept>

namespace flow_control
{

GoBackNSender::GoBackNSender(
  std::size_t frame_count,
  std::size_t window_size,
  std::uint8_t start_sequence
)
  : frame_count_{frame_count},
    window_size_{window_size},
    start_sequence_{start_sequence},
    base_{0U},
    next_to_send_{0U},
    retransmit_pending_{false}
{
  if (window_size_ == 0U || window_size_ > GO_BACK_N_MAX_WINDOW) {
    throw std::invalid_argument("Go-Back-N window must be 1..255");
  }
}

std::uint8_t GoBackNSender::sequence_of(std::size_t frame_index) const noexcept
{
  return static_cast<std::uint8_t>(start_sequence_ + frame_index);
}

std::vector<Transmission> GoBackNSender::transmissions()
{
  std::vector<Transmission> result;

  if (retransmit_pending_) {
    retransmit_pending_ = false;
    for (std::size_t i = base_; i < next_to_send_; ++i) {
      result.push_back(Transmission{i, sequence_of(i), true});
    }
  }

  while (next_to_send_ < frame_count_
         && (next_to_send_ - base_) < window_size_) {
    result.push_back(Transmission{next_to_send_, sequence_of(next_to_send_), false});
    ++next_to_send_;
  }

  return result;
}

bool GoBackNSender::acknowledge(std::uint8_t sequence)
{
  for (std::size_t i = base_; i < next_to_send_; ++i) {
    if (sequence_of(i) == sequence) {
      base_ = i + 1U;
      retransmit_pending_ = false;
      return true;
    }
  }
  return false;
}

void GoBackNSender::timeout()
{
  if (base_ < next_to_send_) {
    retransmit_pending_ = true;
  }
}

bool GoBackNSender::complete() const noexcept
{
  return base_ >= frame_count_;
}

GoBackNReceiver::GoBackNReceiver(std::uint8_t start_sequence)
  : expected_sequence_{start_sequence},
    last_acked_sequence_{start_sequence},
    has_received_{false}
{
}

ReceiveResult GoBackNReceiver::receive(
  std::size_t frame_index,
  std::uint8_t sequence
)
{
  ReceiveResult result{};

  if (sequence == expected_sequence_) {
    result.ack = sequence;
    result.delivered_indices.push_back(frame_index);
    last_acked_sequence_ = sequence;
    has_received_ = true;
    expected_sequence_ = static_cast<std::uint8_t>(expected_sequence_ + 1U);
    return result;
  }

  if (has_received_ && sequence == last_acked_sequence_) {
    result.ack = last_acked_sequence_;
    result.duplicate = true;
    return result;
  }

  result.out_of_order = true;
  if (has_received_) {
    result.ack = last_acked_sequence_;
  }
  return result;
}

}  // namespace flow_control
