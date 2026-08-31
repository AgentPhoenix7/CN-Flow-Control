#include "record.hpp"
#include "socket.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{

int test_move_ownership()
{
  auto sockets = flow_control::socket_pair();
  flow_control::Socket moved = std::move(sockets.first);

  if (sockets.first.valid() || !moved.valid() || !sockets.second.valid()) {
    std::cerr << "FAIL: socket move ownership\n";
    return 1;
  }

  std::cout << "PASS: socket move ownership\n";
  return 0;
}

int test_record_exchange()
{
  auto sockets = flow_control::socket_pair();
  const auto sent = flow_control::make_ack_record(0x42U);
  sockets.first.send_record(sent);
  const auto received = sockets.second.receive_record();

  if (!received.has_value()
      || received->type != sent.type
      || received->body != sent.body) {
    std::cerr << "FAIL: exact record exchange\n";
    return 1;
  }

  std::cout << "PASS: exact record exchange\n";
  return 0;
}

int test_clean_eof()
{
  auto sockets = flow_control::socket_pair();
  sockets.first.close();
  if (sockets.second.receive_record().has_value()) {
    std::cerr << "FAIL: clean EOF returned a record\n";
    return 1;
  }

  std::cout << "PASS: clean socket EOF\n";
  return 0;
}

int test_truncated_record()
{
  auto sockets = flow_control::socket_pair();
  sockets.first.send_all({0x00U, 0x03U, 0x03U});
  sockets.first.close();

  try {
    (void)sockets.second.receive_record();
  } catch (const std::runtime_error&) {
    std::cout << "PASS: truncated record rejection\n";
    return 0;
  }

  std::cerr << "FAIL: truncated record accepted\n";
  return 1;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += test_move_ownership();
  failures += test_record_exchange();
  failures += test_clean_eof();
  failures += test_truncated_record();
  return failures == 0 ? 0 : 1;
}
