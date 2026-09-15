#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "channel.hpp"
#include "go_back_n.hpp"
#include "run_stats.hpp"
#include "selective_repeat.hpp"
#include "socket.hpp"
#include "stop_and_wait.hpp"

namespace {

struct Args {
  std::string protocol;
  std::string host = "127.0.0.1";
  std::uint16_t port = 5000;
  std::string input_path;
  int window = 1;
  double data_error = 0.0;
  double data_loss = 0.0;
  unsigned seed = 1;
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
    } else if (flag == "--host") {
      args.host = value();
    } else if (flag == "--port") {
      args.port = static_cast<std::uint16_t>(std::stoi(value()));
    } else if (flag == "--input") {
      args.input_path = value();
    } else if (flag == "--window") {
      args.window = std::stoi(value());
    } else if (flag == "--data-error") {
      args.data_error = std::stod(value());
    } else if (flag == "--data-loss") {
      args.data_loss = std::stod(value());
    } else if (flag == "--seed") {
      args.seed = static_cast<unsigned>(std::stoi(value()));
    } else {
      throw std::invalid_argument("unknown flag: " + flag);
    }
  }
  if (args.protocol.empty()) throw std::invalid_argument("--protocol is required");
  if (args.input_path.empty()) throw std::invalid_argument("--input is required");
  return args;
}

std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open input file: " + path);
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void print_stats(const std::string& protocol, const basic_flow::RunStats& stats) {
  std::cout << "protocol=" << protocol << " elapsed_ms=" << stats.elapsed_ms
            << " frames_sent=" << stats.frames_sent << " retransmissions=" << stats.retransmissions
            << " acks_received=" << stats.acks_received << " timeouts=" << stats.timeouts << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    Args args = parse_args(argc, argv);
    auto file_bytes = read_file(args.input_path);
    auto socket = basic_flow::TcpSocket::connect_to(args.host, args.port);
    basic_flow::ChannelConfig data_cfg{args.data_error, args.data_loss};

    basic_flow::RunStats stats;
    if (args.protocol == "stop-and-wait") {
      stats = basic_flow::run_stop_and_wait_sender(socket, file_bytes, data_cfg, args.seed);
    } else if (args.protocol == "go-back-n") {
      stats = basic_flow::run_go_back_n_sender(socket, file_bytes, args.window, data_cfg, args.seed);
    } else if (args.protocol == "selective-repeat") {
      stats = basic_flow::run_selective_repeat_sender(socket, file_bytes, args.window, data_cfg, args.seed);
    } else {
      throw std::invalid_argument("unknown --protocol: " + args.protocol);
    }
    print_stats(args.protocol, stats);
  } catch (const std::exception& e) {
    std::cerr << "sender: " << e.what() << "\n";
    return 1;
  }
  return 0;
}

/*
PARSE ARGUMENTS:

set default values:
  host = 127.0.0.1
  port = 5000
  window = 1
  data_error = 0
  data_loss = 0
  seed = 1

for each command-line flag:

  if flag requires a value but no value exists:
    report error

  if flag == --protocol:
    store protocol

  else if flag == --host:
    store host

  else if flag == --port:
    store port

  else if flag == --input:
    store input file path

  else if flag == --window:
    store window size

  else if flag == --data-error:
    store data error probability

  else if flag == --data-loss:
    store data loss probability

  else if flag == --seed:
    store random seed

  else:
    report unknown flag

if protocol is missing:
  report error

if input file is missing:
  report error
*/

/*
READ FILE:

open input file in binary mode

if file cannot be opened:
  report error

read entire file into byte array

return byte array
*/

/*
MAIN SENDER:

parse command-line arguments

read input file into memory

connect to receiver

create DATA channel configuration using:
  data error probability
  data loss probability


if protocol == stop-and-wait:

  run Stop-and-Wait sender


else if protocol == go-back-n:

  run Go-Back-N sender using specified window


else if protocol == selective-repeat:

  run Selective Repeat sender using specified window


else:

  report unknown protocol


print:
  protocol
  elapsed time
  frames sent
  retransmissions
  ACKs received
  timeouts


if any exception occurs:
  print error
  return failure

return success
*/