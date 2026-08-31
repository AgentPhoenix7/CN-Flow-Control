#include "stop_and_wait.hpp"

namespace flow_control
{

StopAndWaitSender::StopAndWaitSender(
  std::size_t frame_count,
  std::uint8_t start_sequence
)
  : frame_count_{frame_count},
    start_sequence_{start_sequence},
    next_index_{0U},
    has_outstanding_{false},
    retransmit_pending_{false}
{
}

std::vector<Transmission> StopAndWaitSender::transmissions()
{
  if (next_index_ >= frame_count_) {
    return {};
  }

  const std::uint8_t sequence = static_cast<std::uint8_t>(
    start_sequence_ + next_index_
  );

  if (!has_outstanding_) {
    has_outstanding_ = true;
    retransmit_pending_ = false;
    return {Transmission{next_index_, sequence, false}};
  }

  if (retransmit_pending_) {
    retransmit_pending_ = false;
    return {Transmission{next_index_, sequence, true}};
  }

  return {};
}

bool StopAndWaitSender::acknowledge(std::uint8_t sequence)
{
  if (!has_outstanding_) {
    return false;
  }

  const std::uint8_t expected = static_cast<std::uint8_t>(
    start_sequence_ + next_index_
  );
  if (sequence != expected) {
    return false;
  }

  has_outstanding_ = false;
  retransmit_pending_ = false;
  ++next_index_;
  return true;
}

void StopAndWaitSender::timeout()
{
  if (has_outstanding_) {
    retransmit_pending_ = true;
  }
}

bool StopAndWaitSender::complete() const noexcept
{
  return next_index_ >= frame_count_;
}

StopAndWaitReceiver::StopAndWaitReceiver()
  : expected_sequence_{0U},
    last_acked_sequence_{0U},
    has_received_{false}
{
}

ReceiveResult StopAndWaitReceiver::receive(
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
  return result;
}

}  // namespace flow_control
