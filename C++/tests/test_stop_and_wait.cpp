#include "stop_and_wait.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{

int test_new_send_and_matching_ack()
{
  flow_control::StopAndWaitSender sender{2U};
  const auto transmissions = sender.transmissions();
  if (transmissions.size() != 1U
      || transmissions[0].frame_index != 0U
      || transmissions[0].sequence != 0U
      || transmissions[0].retransmission
      || sender.acknowledge(1U)
      || !sender.acknowledge(0U)
      || sender.complete()) {
    std::cerr << "FAIL: Stop-and-Wait send/ACK\n";
    return 1;
  }
  std::cout << "PASS: Stop-and-Wait send and ACK\n";
  return 0;
}

int test_timeout_retransmission()
{
  flow_control::StopAndWaitSender sender{1U};
  (void)sender.transmissions();
  sender.timeout();
  const auto retransmissions = sender.transmissions();
  if (retransmissions.size() != 1U
      || !retransmissions[0].retransmission
      || retransmissions[0].frame_index != 0U) {
    std::cerr << "FAIL: Stop-and-Wait timeout\n";
    return 1;
  }
  std::cout << "PASS: Stop-and-Wait timeout retransmission\n";
  return 0;
}

int test_receiver_duplicate_delivery()
{
  flow_control::StopAndWaitReceiver receiver;
  const auto first = receiver.receive(0U, 0U);
  const auto duplicate = receiver.receive(0U, 0U);
  if (!first.ack.has_value() || *first.ack != 0U
      || first.delivered_indices != std::vector<std::size_t>{0U}
      || duplicate.delivered_indices.size() != 0U
      || !duplicate.duplicate
      || !duplicate.ack.has_value() || *duplicate.ack != 0U) {
    std::cerr << "FAIL: Stop-and-Wait duplicate receiver\n";
    return 1;
  }
  std::cout << "PASS: Stop-and-Wait duplicate-safe receiver\n";
  return 0;
}

int test_out_of_order_rejection()
{
  flow_control::StopAndWaitReceiver receiver;
  const auto result = receiver.receive(1U, 1U);
  if (result.ack.has_value() || !result.out_of_order
      || !result.delivered_indices.empty()) {
    std::cerr << "FAIL: Stop-and-Wait out-of-order receiver\n";
    return 1;
  }
  std::cout << "PASS: Stop-and-Wait out-of-order rejection\n";
  return 0;
}

int test_sequence_wraparound()
{
  flow_control::StopAndWaitSender sender{2U, 255U};
  const auto first = sender.transmissions();
  if (first[0].sequence != 255U || !sender.acknowledge(255U)) {
    std::cerr << "FAIL: Stop-and-Wait pre-wrap sequence\n";
    return 1;
  }
  const auto second = sender.transmissions();
  if (second[0].sequence != 0U || !sender.acknowledge(0U)
      || !sender.complete()) {
    std::cerr << "FAIL: Stop-and-Wait sequence wrap\n";
    return 1;
  }
  std::cout << "PASS: Stop-and-Wait sequence wraparound\n";
  return 0;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += test_new_send_and_matching_ack();
  failures += test_timeout_retransmission();
  failures += test_receiver_duplicate_delivery();
  failures += test_out_of_order_rejection();
  failures += test_sequence_wraparound();
  return failures == 0 ? 0 : 1;
}
