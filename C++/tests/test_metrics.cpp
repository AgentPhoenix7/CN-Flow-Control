#include "metrics.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace
{

bool near(double first, double second)
{
  return std::abs(first - second) < 0.000001;
}

int test_zero_denominators()
{
  const flow_control::Metrics metrics{};
  if (metrics.efficiency() != 0.0
      || metrics.goodput_bytes_per_second() != 0.0
      || metrics.mean_rtt_ms() != 0.0) {
    std::cerr << "FAIL: zero metric denominators\n";
    return 1;
  }

  std::cout << "PASS: zero metric denominators\n";
  return 0;
}

int test_derived_metrics()
{
  flow_control::Metrics metrics{};
  metrics.unique_payload_bytes = 500U;
  metrics.transmitted_frame_bytes = 1000U;
  metrics.completion_ms = 250U;
  metrics.rtt_sample_count = 2U;
  metrics.total_rtt_ms = 60U;

  if (!near(metrics.efficiency(), 0.5)
      || !near(metrics.goodput_bytes_per_second(), 2000.0)
      || !near(metrics.mean_rtt_ms(), 30.0)) {
    std::cerr << "FAIL: derived metrics\n";
    return 1;
  }

  std::cout << "PASS: derived metrics\n";
  return 0;
}

int test_csv_serialization()
{
  flow_control::Metrics metrics{};
  metrics.unique_payload_bytes = 5U;
  metrics.transmitted_frame_bytes = 10U;
  metrics.original_transmissions = 1U;
  metrics.retransmissions = 2U;
  metrics.acks = 3U;
  metrics.timeouts = 4U;
  metrics.duplicates = 5U;
  metrics.out_of_order = 6U;
  metrics.completion_ms = 10U;
  metrics.rtt_sample_count = 2U;
  metrics.total_rtt_ms = 8U;
  metrics.current_timeout_ms = 100U;

  const std::string header = flow_control::metrics_csv_header();
  const std::string row = flow_control::metrics_csv_row(metrics);
  if (header.find("efficiency") == std::string::npos
      || header.find("goodput_bytes_per_second") == std::string::npos
      || row != "5,10,1,2,3,4,5,6,10,2,8,100,0.500000,500.000000,4.000000") {
    std::cerr << "FAIL: metrics CSV serialization\n";
    return 1;
  }

  std::cout << "PASS: metrics CSV serialization\n";
  return 0;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += test_zero_denominators();
  failures += test_derived_metrics();
  failures += test_csv_serialization();
  return failures == 0 ? 0 : 1;
}
