#include "channel.hpp"
#include "config.hpp"
#include "frame.hpp"
#include "go_back_n.hpp"
#include "metrics.hpp"
#include "protocol.hpp"
#include "record.hpp"
#include "selective_repeat.hpp"
#include "session.hpp"
#include "socket.hpp"
#include "stop_and_wait.hpp"
#include "timer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace flow_control
{

namespace
{

/** Locally administered source address used by every simulated frame. */
constexpr std::array<std::uint8_t, MAC_ADDRESS_SIZE> SENDER_MAC{
  0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U
};
/** Locally administered destination address used by every simulated frame. */
constexpr std::array<std::uint8_t, MAC_ADDRESS_SIZE> RECEIVER_MAC{
  0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U
};

/** Rounds allowed per frame before a transfer is declared non-convergent. */
constexpr std::size_t ROUND_BUDGET_PER_FRAME = 200U;
/** Rounds always allowed, so tiny transfers still tolerate impairment. */
constexpr std::size_t MINIMUM_ROUND_BUDGET = 1000U;

const char* const USAGE =
  "usage: sender --protocol <stop-and-wait|go-back-n|selective-repeat>\n"
  "              --input <path> --port <1-65535>\n"
  "              [--fcs <checksum16|crc8|crc10|crc16|crc32>]\n"
  "              [--host <host>] [--window <1-255>] [--payload <1-1499>]\n"
  "              [--data-error <0.0-1.0>] [--data-delay <0.0-1.0>]\n"
  "              [--ack-error <0.0-1.0>] [--ack-delay <0.0-1.0>]\n"
  "              [--seed <unsigned>]\n";

/** Validated sender command-line configuration. */
struct Options
{
  ArqProtocol protocol{ArqProtocol::StopAndWait};
  FcsScheme fcs{FcsScheme::Checksum16};
  std::string input_path;
  std::string host{"127.0.0.1"};
  std::uint16_t port{0U};
  std::size_t window_size{1U};
  std::size_t payload_size{DEFAULT_PAYLOAD_SIZE};
  double data_error{0.0};
  double data_delay{0.0};
  double ack_error{0.0};
  double ack_delay{0.0};
  std::uint32_t seed{1U};
};

/** What the sender knows about one frame it has already put on the wire. */
struct FrameTiming
{
  std::uint64_t sent_logical_ms{0U};
  bool sent{false};
  bool retransmitted{false};
};

/** Uniform interface over the three ARQ sender state machines. */
class ArqSender
{
public:
  ArqSender() = default;
  virtual ~ArqSender() = default;

  ArqSender(const ArqSender&) = delete;
  ArqSender& operator=(const ArqSender&) = delete;
  ArqSender(ArqSender&&) = delete;
  ArqSender& operator=(ArqSender&&) = delete;

  virtual std::vector<Transmission> transmissions() = 0;
  virtual bool acknowledge(std::uint8_t sequence) = 0;
  virtual void timeout() = 0;
  virtual bool complete() const = 0;
};

/** Adapts one concrete ARQ sender state machine to ArqSender. */
template <typename Machine>
class ArqSenderAdapter final : public ArqSender
{
public:
  template <typename... Arguments>
  explicit ArqSenderAdapter(Arguments&&... arguments)
    : machine_(std::forward<Arguments>(arguments)...)
  {
  }

  std::vector<Transmission> transmissions() override
  {
    return machine_.transmissions();
  }

  bool acknowledge(std::uint8_t sequence) override
  {
    return machine_.acknowledge(sequence);
  }

  void timeout() override
  {
    machine_.timeout();
  }

  bool complete() const override
  {
    return machine_.complete();
  }

private:
  Machine machine_;
};

std::unique_ptr<ArqSender> make_arq_sender(
  ArqProtocol protocol,
  std::size_t frame_count,
  std::size_t window_size
)
{
  switch (protocol) {
    case ArqProtocol::StopAndWait:
      return std::make_unique<ArqSenderAdapter<StopAndWaitSender>>(
        frame_count
      );
    case ArqProtocol::GoBackN:
      return std::make_unique<ArqSenderAdapter<GoBackNSender>>(
        frame_count, window_size
      );
    case ArqProtocol::SelectiveRepeat:
      return std::make_unique<ArqSenderAdapter<SelectiveRepeatSender>>(
        frame_count, window_size
      );
  }

  throw std::invalid_argument("unsupported ARQ protocol");
}

ArqProtocol parse_protocol(const std::string& name)
{
  if (name == "stop-and-wait") {
    return ArqProtocol::StopAndWait;
  }
  if (name == "go-back-n") {
    return ArqProtocol::GoBackN;
  }
  if (name == "selective-repeat") {
    return ArqProtocol::SelectiveRepeat;
  }

  throw std::invalid_argument("unknown protocol name: " + name);
}

FcsScheme parse_fcs(const std::string& name)
{
  if (name == "checksum16") {
    return FcsScheme::Checksum16;
  }
  if (name == "crc8") {
    return FcsScheme::Crc8;
  }
  if (name == "crc10") {
    return FcsScheme::Crc10;
  }
  if (name == "crc16") {
    return FcsScheme::Crc16;
  }
  if (name == "crc32") {
    return FcsScheme::Crc32;
  }

  throw std::invalid_argument("unknown FCS scheme name: " + name);
}

unsigned long long parse_unsigned(
  const std::string& text,
  const std::string& option,
  unsigned long long minimum,
  unsigned long long maximum
)
{
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
    throw std::invalid_argument(option + " requires a non-negative integer");
  }

  unsigned long long value = 0U;
  try {
    std::size_t consumed = 0U;
    value = std::stoull(text, &consumed);
    if (consumed != text.size()) {
      throw std::invalid_argument("trailing characters");
    }
  } catch (const std::exception&) {
    throw std::invalid_argument(option + " is not a valid integer: " + text);
  }

  if (value < minimum || value > maximum) {
    throw std::invalid_argument(
      option + " must be between " + std::to_string(minimum) + " and "
      + std::to_string(maximum) + ": " + text
    );
  }

  return value;
}

double parse_probability(const std::string& text, const std::string& option)
{
  double value = 0.0;
  try {
    std::size_t consumed = 0U;
    value = std::stod(text, &consumed);
    if (consumed != text.size()) {
      throw std::invalid_argument("trailing characters");
    }
  } catch (const std::exception&) {
    throw std::invalid_argument(option + " is not a valid number: " + text);
  }

  // Reuses the CONFIG record's range rule so both endpoints agree exactly.
  (void)permyriad_from_probability(value);
  return value;
}

Options parse_options(int argc, char* argv[])
{
  Options options;
  bool has_protocol = false;
  bool has_input = false;
  bool has_port = false;
  bool has_window = false;

  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option.rfind("--", 0U) != 0U) {
      throw std::invalid_argument("unexpected argument: " + option);
    }
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " + option);
    }

    const std::string value = argv[++index];
    if (option == "--protocol") {
      options.protocol = parse_protocol(value);
      has_protocol = true;
    } else if (option == "--fcs") {
      options.fcs = parse_fcs(value);
    } else if (option == "--input") {
      options.input_path = value;
      has_input = true;
    } else if (option == "--host") {
      options.host = value;
    } else if (option == "--port") {
      options.port = static_cast<std::uint16_t>(
        parse_unsigned(value, option, 1U, 65535U)
      );
      has_port = true;
    } else if (option == "--window") {
      options.window_size = static_cast<std::size_t>(
        parse_unsigned(value, option, 1U, GO_BACK_N_MAX_WINDOW)
      );
      has_window = true;
    } else if (option == "--payload") {
      options.payload_size = static_cast<std::size_t>(
        parse_unsigned(value, option, 1U, MAX_PAYLOAD_SIZE)
      );
    } else if (option == "--data-error") {
      options.data_error = parse_probability(value, option);
    } else if (option == "--data-delay") {
      options.data_delay = parse_probability(value, option);
    } else if (option == "--ack-error") {
      options.ack_error = parse_probability(value, option);
    } else if (option == "--ack-delay") {
      options.ack_delay = parse_probability(value, option);
    } else if (option == "--seed") {
      options.seed = static_cast<std::uint32_t>(
        parse_unsigned(value, option, 0U, 0xFFFFFFFFULL)
      );
    } else {
      throw std::invalid_argument("unknown option: " + option);
    }
  }

  if (!has_protocol || !has_input || !has_port) {
    throw std::invalid_argument(
      "--protocol, --input, and --port are required"
    );
  }

  switch (options.protocol) {
    case ArqProtocol::StopAndWait:
      // Stop-and-Wait keeps exactly one frame outstanding by definition.
      options.window_size = 1U;
      break;
    case ArqProtocol::GoBackN:
      if (!has_window) {
        options.window_size = 1U;
      }
      break;
    case ArqProtocol::SelectiveRepeat:
      if (!has_window) {
        options.window_size = 1U;
      }
      if (options.window_size > SELECTIVE_REPEAT_MAX_WINDOW) {
        throw std::invalid_argument(
          "--window must be at most "
          + std::to_string(SELECTIVE_REPEAT_MAX_WINDOW)
          + " for selective-repeat"
        );
      }
      break;
  }

  return options;
}

SessionConfig build_session_config(const Options& options)
{
  return SessionConfig{
    options.protocol,
    options.fcs,
    static_cast<std::uint16_t>(options.window_size),
    static_cast<std::uint16_t>(options.payload_size),
    permyriad_from_probability(options.data_error),
    permyriad_from_probability(options.data_delay),
    permyriad_from_probability(options.ack_error),
    permyriad_from_probability(options.ack_delay),
    options.seed,
    options.seed ^ 0x9E3779B9U
  };
}

std::vector<std::uint8_t> read_file(const std::string& path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open input file: " + path);
  }

  std::vector<std::uint8_t> bytes(
    (std::istreambuf_iterator<char>(input)),
    std::istreambuf_iterator<char>()
  );
  if (input.bad()) {
    throw std::runtime_error("failed to read input file: " + path);
  }

  return bytes;
}

/** Milliseconds elapsed since a reference point, never reported as zero. */
std::uint64_t elapsed_ms_since(
  const std::chrono::steady_clock::time_point& start
)
{
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - start
  ).count();
  const auto measured = elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0U;
  return std::max<std::uint64_t>(1U, measured);
}

/**
 * Drives one deterministic timeout round: transmit the protocol window
 * through the DATA channel, close it with ROUND_END, then collect every
 * ACK the receiver puts on the wire until ACK_END.
 *
 * There are no threads and no wall-clock sleeps. A round that produces no
 * window progress is by definition a timeout, so the round advances a
 * logical clock by the current estimator value before asking the protocol
 * state machine for its retransmissions. Every other round advances that
 * clock by its measured duration, with a one-millisecond floor so RTT
 * samples stay positive on a loopback carrier.
 *
 * Metrics recorded here are the sender's own observations: `duplicates`
 * counts acknowledgments that passed their integrity check but moved no
 * window, and `out_of_order` counts accepted acknowledgments for a frame
 * other than the current window base.
 */
class SenderSession
{
public:
  SenderSession(
    const SessionConfig& config,
    std::vector<std::uint8_t> file_bytes,
    Socket connection
  )
    : config_{config},
      file_{std::move(file_bytes)},
      connection_{std::move(connection)},
      data_channel_{data_channel_config(config)},
      frame_count_{frame_count_for(file_.size(), config.payload_size)},
      machine_{make_arq_sender(
        config.protocol, frame_count_, config.window_size
      )},
      timings_(frame_count_),
      acknowledged_(frame_count_, false)
  {
    sequence_owner_.fill(frame_count_);
  }

  const Metrics& run()
  {
    const std::size_t round_budget =
      MINIMUM_ROUND_BUDGET + (frame_count_ * ROUND_BUDGET_PER_FRAME);
    std::size_t rounds = 0U;

    while (!machine_->complete()) {
      if (++rounds > round_budget) {
        throw std::runtime_error(
          "transfer did not converge within its round budget"
        );
      }
      run_round();
    }

    finish();
    return metrics_;
  }

private:
  void run_round()
  {
    const auto round_start = std::chrono::steady_clock::now();

    for (const Transmission& transmission : machine_->transmissions()) {
      transmit(transmission);
    }
    connection_.send_record(Record{RecordType::RoundEnd, {}});

    const std::vector<std::uint8_t> sequences = collect_acks();
    logical_ms_ += elapsed_ms_since(round_start);

    bool progress = false;
    for (const std::uint8_t sequence : sequences) {
      ++metrics_.acks;
      if (!machine_->acknowledge(sequence)) {
        // A stale or repeated acknowledgment carries no new window progress.
        ++metrics_.duplicates;
        continue;
      }
      progress = true;
      record_progress(sequence);
    }

    if (!progress) {
      // No window movement this round, so every outstanding frame timed out.
      ++metrics_.timeouts;
      logical_ms_ +=
        static_cast<std::uint64_t>(estimator_.timeout().count());
      machine_->timeout();
    }
  }

  void transmit(const Transmission& transmission)
  {
    const std::size_t offset = transmission.frame_index * config_.payload_size;
    const std::size_t length = std::min(
      static_cast<std::size_t>(config_.payload_size), file_.size() - offset
    );
    const std::vector<std::uint8_t> payload(
      file_.begin() + static_cast<std::ptrdiff_t>(offset),
      file_.begin() + static_cast<std::ptrdiff_t>(offset + length)
    );
    const FrameHeader header{
      SENDER_MAC,
      RECEIVER_MAC,
      static_cast<std::uint16_t>(length),
      transmission.sequence
    };
    const std::vector<std::uint8_t> wire = serialize_frame(
      header, payload, config_.fcs
    );

    FrameTiming& timing = timings_[transmission.frame_index];
    metrics_.transmitted_frame_bytes += wire.size();
    if (transmission.retransmission) {
      ++metrics_.retransmissions;
      timing.retransmitted = true;
    } else {
      ++metrics_.original_transmissions;
      timing.sent_logical_ms = logical_ms_;
      timing.sent = true;
    }
    sequence_owner_[transmission.sequence] = transmission.frame_index;

    const ChannelResult delivered = data_channel_.transmit(wire);
    switch (delivered.outcome) {
      case ChannelOutcome::Dropped:
      case ChannelOutcome::Delayed:
        // Suppressed for this round, so the frame never reaches the wire.
        return;
      case ChannelOutcome::Clean:
      case ChannelOutcome::Corrupted:
        connection_.send_record(Record{RecordType::Data, delivered.bytes});
        return;
    }
  }

  std::vector<std::uint8_t> collect_acks()
  {
    std::vector<std::uint8_t> sequences;

    while (true) {
      const ReceivedRecord received = connection_.receive_framed_record();
      if (received.status == ReceiveStatus::CleanEof) {
        throw std::runtime_error("receiver closed the connection mid-round");
      }
      if (received.status == ReceiveStatus::Malformed) {
        // A corrupted ACK fails its complement check and is discarded.
        continue;
      }
      if (received.record.type == RecordType::AckEnd) {
        return sequences;
      }
      if (received.record.type != RecordType::Ack) {
        throw std::runtime_error("unexpected record type in an ACK round");
      }

      const std::optional<std::uint8_t> sequence =
        ack_sequence(received.record);
      if (sequence.has_value()) {
        sequences.push_back(*sequence);
      }
    }
  }

  void record_progress(std::uint8_t sequence)
  {
    const std::size_t frame_index = sequence_owner_[sequence];
    if (frame_index >= frame_count_) {
      return;
    }

    if (frame_index != acknowledged_base_) {
      // The window base is still outstanding, so this ACK arrived early.
      ++metrics_.out_of_order;
    }
    acknowledged_[frame_index] = true;
    if (config_.protocol == ArqProtocol::GoBackN) {
      for (std::size_t index = acknowledged_base_; index < frame_index;
           ++index) {
        acknowledged_[index] = true;
      }
    }
    while (acknowledged_base_ < frame_count_
           && acknowledged_[acknowledged_base_]) {
      ++acknowledged_base_;
    }

    const FrameTiming& timing = timings_[frame_index];
    if (!timing.sent) {
      return;
    }

    const std::uint64_t sample = logical_ms_ - timing.sent_logical_ms;
    estimator_.observe(
      std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(
        sample
      )},
      timing.retransmitted
    );
    if (!timing.retransmitted) {
      ++metrics_.rtt_sample_count;
      metrics_.total_rtt_ms += sample;
    }
  }

  void finish()
  {
    const auto exchange_start = std::chrono::steady_clock::now();
    connection_.send_record(Record{RecordType::Complete, {}});

    const ReceivedRecord received = connection_.receive_framed_record();
    if (received.status != ReceiveStatus::Received
        || received.record.type != RecordType::CompleteAck) {
      throw std::runtime_error("receiver did not acknowledge completion");
    }
    logical_ms_ += elapsed_ms_since(exchange_start);

    metrics_.unique_payload_bytes = file_.size();
    metrics_.completion_ms = logical_ms_;
    metrics_.current_timeout_ms =
      static_cast<std::uint64_t>(estimator_.timeout().count());
  }

  SessionConfig config_;
  std::vector<std::uint8_t> file_;
  Socket connection_;
  Channel data_channel_;
  std::size_t frame_count_;
  std::unique_ptr<ArqSender> machine_;
  std::vector<FrameTiming> timings_;
  std::vector<bool> acknowledged_;
  // Maps a one-byte wire sequence to the frame index that most recently
  // transmitted it, overwritten on every transmit; used only for RTT-sample
  // bookkeeping in record_progress(). Both ARQ constructors reject a window
  // above the protocol limit (GO_BACK_N_MAX_WINDOW = 255, or
  // SELECTIVE_REPEAT_MAX_WINDOW = 128), which is well under the 256 sequence
  // values a single byte can hold, so a sequence number cannot wrap back onto
  // a frame that is still outstanding: retransmitting it requires the whole
  // window to have advanced past its original owner first, which requires
  // that owner to already be acknowledged. A same-value collision in this
  // array therefore cannot happen while the prior owner is still
  // unacknowledged; if the invariant were ever violated, the effect would be
  // silently skipping one RTT sample, not corrupting the transfer, since this
  // array feeds metrics only.
  std::array<std::size_t, 256U> sequence_owner_{};
  std::size_t acknowledged_base_{0U};
  TimeoutEstimator estimator_{};
  Metrics metrics_{};
  std::uint64_t logical_ms_{0U};
};

int run(int argc, char* argv[])
{
  const Options options = parse_options(argc, argv);
  const SessionConfig config = build_session_config(options);
  const Record config_record = make_config_record(config);
  const std::vector<std::uint8_t> file = read_file(options.input_path);

  Socket connection = connect_tcp(options.host, options.port);
  disable_carrier_buffering(connection);
  connection.send_record(config_record);

  SenderSession session{config, file, std::move(connection)};
  std::cout << metrics_csv_row(session.run()) << '\n';
  return 0;
}

}  // namespace

}  // namespace flow_control

int main(int argc, char* argv[])
{
  try {
    return flow_control::run(argc, argv);
  } catch (const std::invalid_argument& error) {
    std::cerr << "sender: " << error.what() << '\n' << flow_control::USAGE;
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "sender: " << error.what() << '\n';
    return 1;
  }
}
