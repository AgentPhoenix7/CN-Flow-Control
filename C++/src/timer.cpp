#include "timer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace flow_control
{

TimeoutEstimator::TimeoutEstimator() noexcept
  : initialized_{false},
    srtt_ms_{0.0},
    rttvar_ms_{0.0},
    timeout_{100}
{
}

std::chrono::milliseconds TimeoutEstimator::timeout() const noexcept
{
  return timeout_;
}

void TimeoutEstimator::observe(
  std::chrono::milliseconds rtt,
  bool retransmitted
)
{
  if (rtt.count() <= 0) {
    throw std::invalid_argument("RTT sample must be positive");
  }

  if (retransmitted) {
    return;
  }

  const double sample = static_cast<double>(rtt.count());
  if (!initialized_) {
    srtt_ms_ = sample;
    rttvar_ms_ = sample / 2.0;
    initialized_ = true;
  } else {
    constexpr double ALPHA = 1.0 / 8.0;
    constexpr double BETA = 1.0 / 4.0;
    rttvar_ms_ = (1.0 - BETA) * rttvar_ms_
      + BETA * std::abs(srtt_ms_ - sample);
    srtt_ms_ = (1.0 - ALPHA) * srtt_ms_ + ALPHA * sample;
  }

  recompute_timeout();
}

void TimeoutEstimator::recompute_timeout() noexcept
{
  constexpr double MIN_TIMEOUT_MS = 10.0;
  constexpr double MAX_TIMEOUT_MS = 2000.0;
  const double calculated = srtt_ms_ + 4.0 * rttvar_ms_;
  const double clamped = std::clamp(
    calculated, MIN_TIMEOUT_MS, MAX_TIMEOUT_MS
  );
  timeout_ = std::chrono::milliseconds{
    static_cast<std::chrono::milliseconds::rep>(std::llround(clamped))
  };
}

}  // namespace flow_control
