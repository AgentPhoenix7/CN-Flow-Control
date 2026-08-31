#include "session.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <sys/socket.h>

namespace flow_control
{

namespace
{

/** Keeps the ACK selection stream independent of the DATA selection stream. */
constexpr std::uint32_t ACK_SELECTION_SEED_MASK = 0xA5A5A5A5U;
/** Keeps the ACK bit-position stream independent of the DATA bit stream. */
constexpr std::uint32_t ACK_BIT_SEED_MASK = 0x5A5A5A5AU;

}  // namespace

double probability_from_permyriad(std::uint16_t permyriad) noexcept
{
  return static_cast<double>(permyriad)
    / static_cast<double>(PROBABILITY_PERMYRIAD_SCALE);
}

std::uint16_t permyriad_from_probability(double probability)
{
  if (!std::isfinite(probability) || probability < 0.0 || probability > 1.0) {
    throw std::invalid_argument("probability must be finite and in [0, 1]");
  }

  const double scaled = probability
    * static_cast<double>(PROBABILITY_PERMYRIAD_SCALE);
  return static_cast<std::uint16_t>(std::llround(scaled));
}

ChannelConfig data_channel_config(const SessionConfig& config) noexcept
{
  return ChannelConfig{
    0.0,
    probability_from_permyriad(config.data_delay_permyriad),
    probability_from_permyriad(config.data_error_permyriad),
    config.selection_seed,
    config.bit_seed
  };
}

ChannelConfig ack_channel_config(const SessionConfig& config) noexcept
{
  return ChannelConfig{
    0.0,
    probability_from_permyriad(config.ack_delay_permyriad),
    probability_from_permyriad(config.ack_error_permyriad),
    config.selection_seed ^ ACK_SELECTION_SEED_MASK,
    config.bit_seed ^ ACK_BIT_SEED_MASK
  };
}

void disable_carrier_buffering(const Socket& socket) noexcept
{
  if (!socket.valid()) {
    return;
  }

  const int enabled = 1;
  (void)::setsockopt(
    socket.native_handle(), IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof enabled
  );
}

std::size_t frame_count_for(
  std::size_t byte_count,
  std::size_t payload_size
)
{
  if (payload_size == 0U) {
    throw std::invalid_argument("payload size must be positive");
  }

  return (byte_count + payload_size - 1U) / payload_size;
}

}  // namespace flow_control
