/**
 * @file session.hpp
 * @brief Declares session helpers shared by the sender and receiver
 * applications.
 *
 * Both applications must derive exactly the same channel probabilities and
 * seeds from the CONFIG record that opens a transfer, so that derivation
 * lives here instead of being duplicated in each executable.
 */

#ifndef FLOW_CONTROL_SESSION_HPP
#define FLOW_CONTROL_SESSION_HPP

#include "channel.hpp"
#include "record.hpp"
#include "socket.hpp"

#include <cstddef>
#include <cstdint>

namespace flow_control
{

/**
 * @brief Stops the TCP carrier from coalescing small round records.
 *
 * Nagle's algorithm holds the second small write of a round until the first
 * is acknowledged, which adds tens of milliseconds of carrier delay to every
 * simulated round trip and distorts the measured RTT. Failures are ignored
 * because the option is only a timing optimization.
 */
void disable_carrier_buffering(const Socket& socket) noexcept;

/** Scale used by the fixed-point probabilities carried in a CONFIG record. */
inline constexpr std::uint16_t PROBABILITY_PERMYRIAD_SCALE = 10000U;

/**
 * @brief Converts parts per ten thousand to a probability in [0, 1].
 */
double probability_from_permyriad(std::uint16_t permyriad) noexcept;

/**
 * @brief Converts a probability in [0, 1] to parts per ten thousand.
 * @throws std::invalid_argument unless the value is finite and in [0, 1].
 */
std::uint16_t permyriad_from_probability(double probability);

/**
 * @brief Derives the DATA-path channel configuration owned by the sender.
 *
 * An excessive-delay outcome models a frame that arrives after the current
 * timeout, so the separate drop probability stays zero and the configured
 * delay probability carries all suppressed deliveries.
 */
ChannelConfig data_channel_config(const SessionConfig& config) noexcept;

/**
 * @brief Derives the ACK-path channel configuration owned by the receiver.
 *
 * The ACK path is seeded independently of the DATA path so that impairment
 * on one direction never shifts the impairment schedule of the other.
 */
ChannelConfig ack_channel_config(const SessionConfig& config) noexcept;

/**
 * @brief Returns the number of fixed-size frames needed for a byte count.
 * @throws std::invalid_argument if payload_size is zero.
 */
std::size_t frame_count_for(
  std::size_t byte_count,
  std::size_t payload_size
);

}  // namespace flow_control

#endif
