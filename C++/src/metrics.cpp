#include "metrics.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace flow_control
{

double Metrics::efficiency() const noexcept
{
  if (transmitted_frame_bytes == 0U) {
    return 0.0;
  }
  return static_cast<double>(unique_payload_bytes)
    / static_cast<double>(transmitted_frame_bytes);
}

double Metrics::goodput_bytes_per_second() const noexcept
{
  if (completion_ms == 0U) {
    return 0.0;
  }
  return static_cast<double>(unique_payload_bytes) * 1000.0
    / static_cast<double>(completion_ms);
}

double Metrics::mean_rtt_ms() const noexcept
{
  if (rtt_sample_count == 0U) {
    return 0.0;
  }
  return static_cast<double>(total_rtt_ms)
    / static_cast<double>(rtt_sample_count);
}

std::string metrics_csv_header()
{
  return "unique_payload_bytes,transmitted_frame_bytes,"
    "original_transmissions,retransmissions,acks,timeouts,duplicates,"
    "out_of_order,completion_ms,rtt_sample_count,total_rtt_ms,"
    "current_timeout_ms,efficiency,goodput_bytes_per_second,mean_rtt_ms";
}

std::string metrics_csv_row(const Metrics& metrics)
{
  std::ostringstream output;
  output
    << metrics.unique_payload_bytes << ','
    << metrics.transmitted_frame_bytes << ','
    << metrics.original_transmissions << ','
    << metrics.retransmissions << ','
    << metrics.acks << ','
    << metrics.timeouts << ','
    << metrics.duplicates << ','
    << metrics.out_of_order << ','
    << metrics.completion_ms << ','
    << metrics.rtt_sample_count << ','
    << metrics.total_rtt_ms << ','
    << metrics.current_timeout_ms << ','
    << std::fixed << std::setprecision(6)
    << metrics.efficiency() << ','
    << metrics.goodput_bytes_per_second() << ','
    << metrics.mean_rtt_ms();
  return output.str();
}

}  // namespace flow_control
