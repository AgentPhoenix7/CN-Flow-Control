#include "go_back_n.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{

int test_window_fill()
{
  flow_control::GoBackNSender sender{5U, 3U};
  const auto sent = sender.transmissions();
  if (sent.size() != 3U
      || sent[0].frame_index != 0U || sent[0].sequence != 0U
      || sent[0].retransmission
      || sent[1].frame_index != 1U || sent[1].sequence != 1U
      || sent[2].frame_index != 2U || sent[2].sequence != 2U) {
    std::cerr << "FAIL: GBN window fill\n";
    return 1;
  }
  const auto again = sender.transmissions();
  if (!again.empty()) {
    std::cerr << "FAIL: GBN window stays full without progress\n";
    return 1;
  }
  std::cout << "PASS: GBN window fill\n";
  return 0;
}

int test_cumulative_ack_slide()
{
  flow_control::GoBackNSender sender{3U, 2U};
  (void)sender.transmissions();
  if (!sender.acknowledge(1U)) {
    std::cerr << "FAIL: GBN cumulative ACK rejected\n";
    return 1;
  }
  const auto opened = sender.transmissions();
  if (opened.size() != 1U
      || opened[0].frame_index != 2U || opened[0].sequence != 2U
      || opened[0].retransmission) {
    std::cerr << "FAIL: GBN cumulative ACK slide\n";
    return 1;
  }
  if (!sender.acknowledge(2U) || !sender.complete()) {
    std::cerr << "FAIL: GBN completion after cumulative ACK\n";
    return 1;
  }
  std::cout << "PASS: GBN cumulative ACK slide\n";
  return 0;
}

int test_whole_window_timeout()
{
  flow_control::GoBackNSender sender{2U, 2U};
  (void)sender.transmissions();
  sender.timeout();
  const auto retransmissions = sender.transmissions();
  if (retransmissions.size() != 2U
      || !retransmissions[0].retransmission
      || retransmissions[0].frame_index != 0U
      || !retransmissions[1].retransmission
      || retransmissions[1].frame_index != 1U) {
    std::cerr << "FAIL: GBN whole-window timeout\n";
    return 1;
  }
  std::cout << "PASS: GBN whole-window timeout retransmission\n";
  return 0;
}

int test_duplicate_ack_rejected()
{
  flow_control::GoBackNSender sender{2U, 2U};
  (void)sender.transmissions();
  if (!sender.acknowledge(0U)) {
    std::cerr << "FAIL: GBN first ACK rejected\n";
    return 1;
  }
  if (sender.acknowledge(0U)) {
    std::cerr << "FAIL: GBN duplicate ACK accepted\n";
    return 1;
  }
  std::cout << "PASS: GBN duplicate ACK rejection\n";
  return 0;
}

int test_max_window_rejected()
{
  try {
    flow_control::GoBackNSender sender{5U, 256U};
    (void)sender;
    std::cerr << "FAIL: GBN window > 255 accepted\n";
    return 1;
  } catch (const std::invalid_argument&) {
    // expected
  }
  try {
    flow_control::GoBackNSender sender{5U, 0U};
    (void)sender;
    std::cerr << "FAIL: GBN window of 0 accepted\n";
    return 1;
  } catch (const std::invalid_argument&) {
    // expected
  }
  std::cout << "PASS: GBN invalid window rejection\n";
  return 0;
}

int test_sequence_wraparound()
{
  flow_control::GoBackNSender sender{2U, 1U, 255U};
  const auto first = sender.transmissions();
  if (first.size() != 1U || first[0].sequence != 255U
      || !sender.acknowledge(255U)) {
    std::cerr << "FAIL: GBN pre-wrap sequence\n";
    return 1;
  }
  const auto second = sender.transmissions();
  if (second.size() != 1U || second[0].sequence != 0U
      || !sender.acknowledge(0U) || !sender.complete()) {
    std::cerr << "FAIL: GBN sequence wrap\n";
    return 1;
  }
  std::cout << "PASS: GBN sequence wraparound\n";
  return 0;
}

int test_receiver_out_of_order_discard()
{
  flow_control::GoBackNReceiver receiver;
  const auto result = receiver.receive(1U, 1U);
  if (result.ack.has_value() || !result.out_of_order
      || !result.delivered_indices.empty()) {
    std::cerr << "FAIL: GBN receiver out-of-order discard\n";
    return 1;
  }
  std::cout << "PASS: GBN receiver out-of-order discard\n";
  return 0;
}

int test_receiver_cumulative_delivery_and_duplicate()
{
  flow_control::GoBackNReceiver receiver;
  const auto first = receiver.receive(0U, 0U);
  const auto second = receiver.receive(1U, 1U);
  const auto duplicate = receiver.receive(1U, 1U);
  if (!first.ack.has_value() || *first.ack != 0U
      || first.delivered_indices != std::vector<std::size_t>{0U}
      || !second.ack.has_value() || *second.ack != 1U
      || second.delivered_indices != std::vector<std::size_t>{1U}
      || duplicate.out_of_order || !duplicate.duplicate
      || !duplicate.ack.has_value() || *duplicate.ack != 1U
      || !duplicate.delivered_indices.empty()) {
    std::cerr << "FAIL: GBN receiver cumulative delivery / duplicate\n";
    return 1;
  }
  std::cout << "PASS: GBN receiver cumulative delivery and duplicate\n";
  return 0;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += test_window_fill();
  failures += test_cumulative_ack_slide();
  failures += test_whole_window_timeout();
  failures += test_duplicate_ack_rejected();
  failures += test_max_window_rejected();
  failures += test_sequence_wraparound();
  failures += test_receiver_out_of_order_discard();
  failures += test_receiver_cumulative_delivery_and_duplicate();
  return failures == 0 ? 0 : 1;
}
