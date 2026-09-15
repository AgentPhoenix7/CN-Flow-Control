#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "channel.hpp"
#include "go_back_n.hpp"
#include "selective_repeat.hpp"
#include "socket.hpp"
#include "stop_and_wait.hpp"

namespace {

struct Args {
  std::string protocol;
  std::uint16_t port = 5000;
  std::string output_path;
  int window = 1;
  double ack_error = 0.0;
  double ack_loss = 0.0;
  unsigned seed = 2;
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string flag = argv[i];
    auto value = [&]() -> std::string {
      if (i + 1 >= argc) throw std::invalid_argument("missing value for " + flag);
      return argv[++i];
    };
    if (flag == "--protocol") {
      args.protocol = value();
    } else if (flag == "--port") {
      args.port = static_cast<std::uint16_t>(std::stoi(value()));
    } else if (flag == "--output") {
      args.output_path = value();
    } else if (flag == "--window") {
      args.window = std::stoi(value());
    } else if (flag == "--ack-error") {
      args.ack_error = std::stod(value());
    } else if (flag == "--ack-loss") {
      args.ack_loss = std::stod(value());
    } else if (flag == "--seed") {
      args.seed = static_cast<unsigned>(std::stoi(value()));
    } else {
      throw std::invalid_argument("unknown flag: " + flag);
    }
  }
  if (args.protocol.empty()) throw std::invalid_argument("--protocol is required");
  if (args.output_path.empty()) throw std::invalid_argument("--output is required");
  return args;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    Args args = parse_args(argc, argv);
    std::cerr << "receiver: listening on port " << args.port << "\n";
    auto socket = basic_flow::TcpSocket::listen_and_accept(args.port);
    std::cerr << "receiver: connected\n";
    basic_flow::ChannelConfig ack_cfg{args.ack_error, args.ack_loss};

    if (args.protocol == "stop-and-wait") {
      basic_flow::run_stop_and_wait_receiver(socket, args.output_path, ack_cfg, args.seed);
    } else if (args.protocol == "go-back-n") {
      basic_flow::run_go_back_n_receiver(socket, args.output_path, ack_cfg, args.seed);
    } else if (args.protocol == "selective-repeat") {
      basic_flow::run_selective_repeat_receiver(socket, args.output_path, args.window, ack_cfg, args.seed);
    } else {
      throw std::invalid_argument("unknown --protocol: " + args.protocol);
    }
    std::cerr << "receiver: transfer complete\n";
  } catch (const std::exception& e) {
    std::cerr << "receiver: " << e.what() << "\n";
    return 1;
  }
  return 0;
}

/*
PARSE ARGUMENTS:

set default values:
  port = 5000
  window = 1
  ack_error = 0
  ack_loss = 0
  seed = 2

for each command-line flag:

  if flag requires a value but no value exists:
    report error

  if flag == --protocol:
    store protocol

  else if flag == --port:
    store port

  else if flag == --output:
    store output file path

  else if flag == --window:
    store window size

  else if flag == --ack-error:
    store ACK error probability

  else if flag == --ack-loss:
    store ACK loss probability

  else if flag == --seed:
    store random seed

  else:
    report unknown flag

if protocol is missing:
  report error

if output file is missing:
  report error
*/

/*
MAIN RECEIVER:

parse command-line arguments

print "listening"

create TCP server
listen on specified port
accept sender connection

print "connected"

create ACK channel configuration using:
  ACK error probability
  ACK loss probability


if protocol == stop-and-wait:

  run Stop-and-Wait receiver


else if protocol == go-back-n:

  run Go-Back-N receiver


else if protocol == selective-repeat:

  run Selective Repeat receiver
    using specified window


else:

  report unknown protocol


print "transfer complete"


if any exception occurs:
  print error
  return failure

return success
*/