#include "selective_repeat.hpp"

#include <stdexcept>

namespace flow_control
{

SelectiveRepeatSender::SelectiveRepeatSender(
  std::size_t frame_count,
  std::size_t window_size,
  std::uint8_t start_sequence
)
  : frame_count_{frame_count},
    window_size_{window_size},
    start_sequence_{start_sequence},
    base_{0U},
    next_to_send_{0U},
    acked_(frame_count, false),
    retransmit_pending_(frame_count, false)
{
  if (window_size_ == 0U || window_size_ > SELECTIVE_REPEAT_MAX_WINDOW) {
    throw std::invalid_argument("Selective Repeat window must be 1..128");
  }
}

std::uint8_t SelectiveRepeatSender::sequence_of(
  std::size_t frame_index
) const noexcept
{
  return static_cast<std::uint8_t>(start_sequence_ + frame_index);
}

std::vector<Transmission> SelectiveRepeatSender::transmissions()
{
  std::vector<Transmission> result;

  for (std::size_t i = base_; i < next_to_send_; ++i) {
    if (retransmit_pending_[i]) {
      retransmit_pending_[i] = false;
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

bool SelectiveRepeatSender::acknowledge(std::uint8_t sequence)
{
  for (std::size_t i = base_; i < next_to_send_; ++i) {
    if (!acked_[i] && sequence_of(i) == sequence) {
      acked_[i] = true;
      retransmit_pending_[i] = false;
      while (base_ < next_to_send_ && acked_[base_]) {
        ++base_;
      }
      return true;
    }
  }
  return false;
}

void SelectiveRepeatSender::timeout()
{
  for (std::size_t i = base_; i < next_to_send_; ++i) {
    if (!acked_[i]) {
      retransmit_pending_[i] = true;
    }
  }
}

bool SelectiveRepeatSender::complete() const noexcept
{
  return base_ >= frame_count_;
}

SelectiveRepeatReceiver::SelectiveRepeatReceiver(std::size_t window_size)
  : window_size_{window_size},
    next_deliver_index_{0U}
{
  if (window_size_ == 0U || window_size_ > SELECTIVE_REPEAT_MAX_WINDOW) {
    throw std::invalid_argument("Selective Repeat window must be 1..128");
  }
}

ReceiveResult SelectiveRepeatReceiver::receive(
  std::size_t frame_index,
  std::uint8_t sequence
)
{
  ReceiveResult result{};

  if (frame_index < next_deliver_index_) {
    result.ack = sequence;
    result.duplicate = true;
    return result;
  }

  if (frame_index >= next_deliver_index_ + window_size_) {
    return result;
  }

  if (buffered_indices_.count(frame_index) > 0U) {
    result.ack = sequence;
    result.duplicate = true;
    result.out_of_order = (frame_index != next_deliver_index_);
    return result;
  }

  result.ack = sequence;

  if (frame_index != next_deliver_index_) {
    buffered_indices_.insert(frame_index);
    result.out_of_order = true;
    return result;
  }

  result.delivered_indices.push_back(frame_index);
  ++next_deliver_index_;
  while (buffered_indices_.count(next_deliver_index_) > 0U) {
    buffered_indices_.erase(next_deliver_index_);
    result.delivered_indices.push_back(next_deliver_index_);
    ++next_deliver_index_;
  }

  return result;
}

}  // namespace flow_control
