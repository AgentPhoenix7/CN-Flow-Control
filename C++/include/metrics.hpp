/**
 * @file metrics.hpp
 * @brief Declares transfer counters and derived evaluation metrics.
 */

#ifndef FLOW_CONTROL_METRICS_HPP
#define FLOW_CONTROL_METRICS_HPP

#include <cstdint>
#include <string>

namespace flow_control
{

/** Counters shared by protocol sessions and experiment output. */
struct Metrics
{
  std::uint64_t unique_payload_bytes{};
  std::uint64_t transmitted_frame_bytes{};
  std::uint64_t original_transmissions{};
  std::uint64_t retransmissions{};
  std::uint64_t acks{};
  std::uint64_t timeouts{};
  std::uint64_t duplicates{};
  std::uint64_t out_of_order{};
  std::uint64_t completion_ms{};
  std::uint64_t rtt_sample_count{};
  std::uint64_t total_rtt_ms{};
  std::uint64_t current_timeout_ms{};

  /** Unique payload bytes divided by transmitted data-frame bytes. */
  double efficiency() const noexcept;
  /** Unique delivered bytes divided by logical completion seconds. */
  double goodput_bytes_per_second() const noexcept;
  /** Arithmetic mean of valid, non-retransmitted RTT samples. */
  double mean_rtt_ms() const noexcept;
};

/** Stable CSV column names for Metrics and its derived values. */
std::string metrics_csv_header();
/** Stable CSV values with derived fields formatted to six decimals. */
std::string metrics_csv_row(const Metrics& metrics);

}  // namespace flow_control

#endif
