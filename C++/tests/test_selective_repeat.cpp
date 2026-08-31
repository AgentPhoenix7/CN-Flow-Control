#include "selective_repeat.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{

int test_independent_ack_slide()
{
  flow_control::SelectiveRepeatSender sender{3U, 3U};
  (void)sender.transmissions();
  if (!sender.acknowledge(1U)) {
    std::cerr << "FAIL: SR independent ACK for frame 1 rejected\n";
    return 1;
  }
  if (sender.complete()) {
    std::cerr << "FAIL: SR reported complete with frame 0 unacked\n";
    return 1;
  }
  if (!sender.acknowledge(0U) || !sender.acknowledge(2U)
      || !sender.complete()) {
    std::cerr << "FAIL: SR independent ACK slide to completion\n";
    return 1;
  }
  std::cout << "PASS: SR independent ACK slide\n";
  return 0;
}

int test_selective_retransmission()
{
  flow_control::SelectiveRepeatSender sender{3U, 3U};
  (void)sender.transmissions();
  if (!sender.acknowledge(1U)) {
    std::cerr << "FAIL: SR ACK for frame 1 rejected\n";
    return 1;
  }
  sender.timeout();
  const auto retransmissions = sender.transmissions();
  if (retransmissions.size() != 2U
      || retransmissions[0].frame_index != 0U
      || !retransmissions[0].retransmission
      || retransmissions[1].frame_index != 2U
      || !retransmissions[1].retransmission) {
    std::cerr << "FAIL: SR selective retransmission\n";
    return 1;
  }
  std::cout << "PASS: SR selective retransmission\n";
  return 0;
}

int test_duplicate_ack_rejected()
{
  flow_control::SelectiveRepeatSender sender{2U, 2U};
  (void)sender.transmissions();
  if (!sender.acknowledge(0U)) {
    std::cerr << "FAIL: SR first ACK rejected\n";
    return 1;
  }
  if (sender.acknowledge(0U)) {
    std::cerr << "FAIL: SR duplicate ACK accepted\n";
    return 1;
  }
  std::cout << "PASS: SR duplicate ACK rejection\n";
  return 0;
}

int test_max_window_rejected()
{
  try {
    flow_control::SelectiveRepeatSender sender{5U, 129U};
    (void)sender;
    std::cerr << "FAIL: SR window > 128 accepted\n";
    return 1;
  } catch (const std::invalid_argument&) {
    // expected
  }
  try {
    flow_control::SelectiveRepeatSender sender{5U, 0U};
    (void)sender;
    std::cerr << "FAIL: SR window of 0 accepted\n";
    return 1;
  } catch (const std::invalid_argument&) {
    // expected
  }
  std::cout << "PASS: SR invalid window rejection\n";
  return 0;
}

int test_receiver_max_window_rejected()
{
  try {
    flow_control::SelectiveRepeatReceiver receiver{129U};
    (void)receiver;
    std::cerr << "FAIL: SR receiver window > 128 accepted\n";
    return 1;
  } catch (const std::invalid_argument&) {
    // expected
  }
  try {
    flow_control::SelectiveRepeatReceiver receiver{0U};
    (void)receiver;
    std::cerr << "FAIL: SR receiver window of 0 accepted\n";
    return 1;
  } catch (const std::invalid_argument&) {
    // expected
  }
  std::cout << "PASS: SR receiver invalid window rejection\n";
  return 0;
}

int test_sequence_wraparound()
{
  flow_control::SelectiveRepeatSender sender{2U, 1U, 255U};
  const auto first = sender.transmissions();
  if (first.size() != 1U || first[0].sequence != 255U
      || !sender.acknowledge(255U)) {
    std::cerr << "FAIL: SR pre-wrap sequence\n";
    return 1;
  }
  const auto second = sender.transmissions();
  if (second.size() != 1U || second[0].sequence != 0U
      || !sender.acknowledge(0U) || !sender.complete()) {
    std::cerr << "FAIL: SR sequence wrap\n";
    return 1;
  }
  std::cout << "PASS: SR sequence wraparound\n";
  return 0;
}

int test_out_of_order_buffering_and_contiguous_delivery()
{
  flow_control::SelectiveRepeatReceiver receiver{3U};
  const auto ahead = receiver.receive(1U, 1U);
  if (!ahead.ack.has_value() || *ahead.ack != 1U || !ahead.out_of_order
      || !ahead.delivered_indices.empty()) {
    std::cerr << "FAIL: SR receiver out-of-order buffering\n";
    return 1;
  }
  const auto filling_gap = receiver.receive(0U, 0U);
  if (!filling_gap.ack.has_value() || *filling_gap.ack != 0U
      || filling_gap.out_of_order
      || filling_gap.delivered_indices != std::vector<std::size_t>{0U, 1U}) {
    std::cerr << "FAIL: SR receiver contiguous delivery\n";
    return 1;
  }
  std::cout << "PASS: SR receiver out-of-order buffering and contiguous delivery\n";
  return 0;
}

int test_receiver_duplicate_ack()
{
  flow_control::SelectiveRepeatReceiver receiver{3U};
  (void)receiver.receive(0U, 0U);
  const auto duplicate = receiver.receive(0U, 0U);
  if (!duplicate.duplicate || !duplicate.ack.has_value()
      || *duplicate.ack != 0U || !duplicate.delivered_indices.empty()) {
    std::cerr << "FAIL: SR receiver duplicate ACK\n";
    return 1;
  }
  std::cout << "PASS: SR receiver duplicate ACK\n";
  return 0;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += test_independent_ack_slide();
  failures += test_selective_retransmission();
  failures += test_duplicate_ack_rejected();
  failures += test_max_window_rejected();
  failures += test_receiver_max_window_rejected();
  failures += test_sequence_wraparound();
  failures += test_out_of_order_buffering_and_contiguous_delivery();
  failures += test_receiver_duplicate_ack();
  return failures == 0 ? 0 : 1;
}
