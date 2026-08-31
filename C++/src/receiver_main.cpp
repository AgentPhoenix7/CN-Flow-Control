#include "channel.hpp"
#include "config.hpp"
#include "frame.hpp"
#include "go_back_n.hpp"
#include "protocol.hpp"
#include "record.hpp"
#include "selective_repeat.hpp"
#include "session.hpp"
#include "socket.hpp"
#include "stop_and_wait.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
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

/**
 * Half of the sequence space. A frame whose sequence sits further ahead than
 * this is interpreted as a repeat of an already delivered frame, which is the
 * only unambiguous reading while every window stays at or below 128.
 */
constexpr std::uint8_t SEQUENCE_LOOKAHEAD_LIMIT = 128U;

const char* const USAGE =
  "usage: receiver --port <1-65535> --output <path>\n";

/** Validated receiver command-line configuration. */
struct Options
{
  std::uint16_t port{0U};
  std::string output_path;
};

/** Uniform interface over the three ARQ receiver state machines. */
class ArqReceiver
{
public:
  ArqReceiver() = default;
  virtual ~ArqReceiver() = default;

  ArqReceiver(const ArqReceiver&) = delete;
  ArqReceiver& operator=(const ArqReceiver&) = delete;
  ArqReceiver(ArqReceiver&&) = delete;
  ArqReceiver& operator=(ArqReceiver&&) = delete;

  virtual ReceiveResult receive(
    std::size_t frame_index,
    std::uint8_t sequence
  ) = 0;
};

/** Adapts one concrete ARQ receiver state machine to ArqReceiver. */
template <typename Machine>
class ArqReceiverAdapter final : public ArqReceiver
{
public:
  template <typename... Arguments>
  explicit ArqReceiverAdapter(Arguments&&... arguments)
    : machine_(std::forward<Arguments>(arguments)...)
  {
  }

  ReceiveResult receive(
    std::size_t frame_index,
    std::uint8_t sequence
  ) override
  {
    return machine_.receive(frame_index, sequence);
  }

private:
  Machine machine_;
};

std::unique_ptr<ArqReceiver> make_arq_receiver(const SessionConfig& config)
{
  switch (config.protocol) {
    case ArqProtocol::StopAndWait:
      return std::make_unique<ArqReceiverAdapter<StopAndWaitReceiver>>();
    case ArqProtocol::GoBackN:
      return std::make_unique<ArqReceiverAdapter<GoBackNReceiver>>();
    case ArqProtocol::SelectiveRepeat:
      return std::make_unique<ArqReceiverAdapter<SelectiveRepeatReceiver>>(
        static_cast<std::size_t>(config.window_size)
      );
  }

  throw std::invalid_argument("unsupported ARQ protocol");
}

std::uint16_t parse_port(const std::string& text)
{
  if (text.empty()
      || text.find_first_not_of("0123456789") != std::string::npos) {
    throw std::invalid_argument("--port requires a non-negative integer");
  }

  unsigned long long value = 0U;
  try {
    value = std::stoull(text);
  } catch (const std::exception&) {
    throw std::invalid_argument("--port is not a valid integer: " + text);
  }
  if (value < 1U || value > 65535U) {
    throw std::invalid_argument("--port must be between 1 and 65535: " + text);
  }

  return static_cast<std::uint16_t>(value);
}

Options parse_options(int argc, char* argv[])
{
  Options options;
  bool has_port = false;
  bool has_output = false;

  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option.rfind("--", 0U) != 0U) {
      throw std::invalid_argument("unexpected argument: " + option);
    }
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " + option);
    }

    const std::string value = argv[++index];
    if (option == "--port") {
      options.port = parse_port(value);
      has_port = true;
    } else if (option == "--output") {
      options.output_path = value;
      has_output = true;
    } else {
      throw std::invalid_argument("unknown option: " + option);
    }
  }

  if (!has_port || !has_output) {
    throw std::invalid_argument("--port and --output are required");
  }

  return options;
}

/**
 * Runs the receiver half of one deterministic timeout round: verify each
 * arriving frame, hand it to the ARQ receiver, write only contiguous
 * delivered payload, and release the round's ACKs through the ACK channel.
 */
class ReceiverSession
{
public:
  ReceiverSession(
    const SessionConfig& config,
    Socket connection,
    const std::string& output_path
  )
    : config_{config},
      connection_{std::move(connection)},
      ack_channel_{ack_channel_config(config)},
      machine_{make_arq_receiver(config)},
      output_{output_path, std::ios::binary | std::ios::trunc}
  {
    if (!output_) {
      throw std::runtime_error("cannot open output file: " + output_path);
    }
  }

  void run()
  {
    while (true) {
      const ReceivedRecord received = connection_.receive_framed_record();
      if (received.status == ReceiveStatus::CleanEof) {
        throw std::runtime_error("sender closed the connection before COMPLETE");
      }
      if (received.status == ReceiveStatus::Malformed) {
        // A record whose own encoding was corrupted cannot be interpreted.
        continue;
      }

      switch (received.record.type) {
        case RecordType::Data:
          handle_data(received.record.body);
          break;
        case RecordType::RoundEnd:
          release_acks();
          break;
        case RecordType::Complete:
          finish();
          return;
        case RecordType::Config:
        case RecordType::Ack:
        case RecordType::AckEnd:
        case RecordType::CompleteAck:
          throw std::runtime_error("unexpected record type from the sender");
      }
    }
  }

private:
  void handle_data(const std::vector<std::uint8_t>& body)
  {
    const std::optional<VerifiedFrame> verified = verify_frame(
      body, config_.fcs
    );
    if (!verified.has_value()) {
      // A frame that fails structure or FCS checks is silently discarded and
      // is never acknowledged, not even negatively.
      return;
    }

    const std::uint8_t sequence = verified->header.sequence;
    const std::optional<std::size_t> frame_index = resolve_frame_index(
      sequence
    );
    if (!frame_index.has_value()) {
      return;
    }

    // S&W/GBN ignore frame_index on every path except an in-order delivery,
    // where it always equals delivered_frames_ regardless of the arriving
    // sequence number, so resolve_frame_index()'s unconditional return of the
    // delivery cursor for those two protocols is always a safe placeholder.
    const ReceiveResult result = machine_->receive(*frame_index, sequence);
    deliver(result, *frame_index, verified->payload);
    if (result.ack.has_value()) {
      pending_acks_.push_back(*result.ack);
    }
  }

  /**
   * Maps a one-byte wire sequence back onto a frame index. Stop-and-Wait and
   * Go-Back-N deliver only the next expected frame, so their index is always
   * the delivered count; Selective Repeat resolves the index modulo 256
   * around that same delivery cursor.
   */
  std::optional<std::size_t> resolve_frame_index(std::uint8_t sequence) const
  {
    if (config_.protocol != ArqProtocol::SelectiveRepeat) {
      return delivered_frames_;
    }

    const auto base_sequence = static_cast<std::uint8_t>(delivered_frames_);
    const auto offset = static_cast<std::uint8_t>(sequence - base_sequence);
    if (offset < SEQUENCE_LOOKAHEAD_LIMIT) {
      return delivered_frames_ + offset;
    }

    const std::size_t behind = 256U - static_cast<std::size_t>(offset);
    if (behind > delivered_frames_) {
      // No frame with this sequence has ever been in the receive window.
      return std::nullopt;
    }
    return delivered_frames_ - behind;
  }

  void deliver(
    const ReceiveResult& result,
    std::size_t frame_index,
    const std::vector<std::uint8_t>& payload
  )
  {
    if (result.delivered_indices.empty()) {
      const bool buffers_ahead =
        config_.protocol == ArqProtocol::SelectiveRepeat
        && result.ack.has_value()
        && result.out_of_order
        && !result.duplicate;
      if (buffers_ahead) {
        buffered_.emplace(frame_index, payload);
      }
      return;
    }

    for (const std::size_t index : result.delivered_indices) {
      if (index == frame_index) {
        write(payload);
      } else {
        const auto buffered = buffered_.find(index);
        if (buffered == buffered_.end()) {
          throw std::runtime_error("delivered frame was never buffered");
        }
        write(buffered->second);
        buffered_.erase(buffered);
      }
      ++delivered_frames_;
    }
  }

  void write(const std::vector<std::uint8_t>& payload)
  {
    output_.write(
      reinterpret_cast<const char*>(payload.data()),
      static_cast<std::streamsize>(payload.size())
    );
    if (!output_) {
      throw std::runtime_error("failed to write the output file");
    }
  }

  void release_acks()
  {
    for (const std::uint8_t sequence : pending_acks_) {
      const Record ack = make_ack_record(sequence);
      const ChannelResult delivered = ack_channel_.transmit(ack.body);
      switch (delivered.outcome) {
        case ChannelOutcome::Dropped:
        case ChannelOutcome::Delayed:
          // Arrives after the sender's timeout, so it never reaches the wire.
          break;
        case ChannelOutcome::Clean:
          connection_.send_record(ack);
          break;
        case ChannelOutcome::Corrupted:
          send_corrupted_ack(delivered.bytes);
          break;
      }
    }

    pending_acks_.clear();
    connection_.send_record(Record{RecordType::AckEnd, {}});
  }

  /**
   * Puts an ACK whose body was corrupted after encoding on the wire. The
   * external length prefix stays intact, so the sender stays synchronized
   * and rejects the record on its complement byte.
   */
  void send_corrupted_ack(const std::vector<std::uint8_t>& body) const
  {
    std::vector<std::uint8_t> external;
    external.reserve(body.size() + 3U);
    const auto length = static_cast<std::uint16_t>(body.size() + 1U);
    external.push_back(static_cast<std::uint8_t>(length >> 8U));
    external.push_back(static_cast<std::uint8_t>(length));
    external.push_back(static_cast<std::uint8_t>(RecordType::Ack));
    external.insert(external.end(), body.begin(), body.end());
    connection_.send_all(external);
  }

  void finish()
  {
    if (!buffered_.empty()) {
      throw std::runtime_error(
        "sender completed while frames were still buffered out of order"
      );
    }

    output_.flush();
    if (!output_) {
      throw std::runtime_error("failed to flush the output file");
    }
    connection_.send_record(Record{RecordType::CompleteAck, {}});
  }

  SessionConfig config_;
  Socket connection_;
  Channel ack_channel_;
  std::unique_ptr<ArqReceiver> machine_;
  std::ofstream output_;
  std::map<std::size_t, std::vector<std::uint8_t>> buffered_{};
  std::vector<std::uint8_t> pending_acks_{};
  std::size_t delivered_frames_{0U};
};

int run(int argc, char* argv[])
{
  const Options options = parse_options(argc, argv);

  const Socket listener = listen_tcp(options.port);
  // Lets a supervising test or experiment runner wait for a bound port.
  std::cerr << "receiver: listening on port " << options.port << '\n';

  Socket connection = listener.accept();
  disable_carrier_buffering(connection);

  const ReceivedRecord opening = connection.receive_framed_record();
  if (opening.status != ReceiveStatus::Received
      || opening.record.type != RecordType::Config) {
    throw std::runtime_error("sender did not open with a CONFIG record");
  }

  const std::optional<SessionConfig> config = session_config(opening.record);
  if (!config.has_value()) {
    throw std::runtime_error("sender sent an invalid session configuration");
  }

  ReceiverSession session{
    *config, std::move(connection), options.output_path
  };
  session.run();
  return 0;
}

}  // namespace

}  // namespace flow_control

int main(int argc, char* argv[])
{
  try {
    return flow_control::run(argc, argv);
  } catch (const std::invalid_argument& error) {
    std::cerr << "receiver: " << error.what() << '\n' << flow_control::USAGE;
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "receiver: " << error.what() << '\n';
    return 1;
  }
}
