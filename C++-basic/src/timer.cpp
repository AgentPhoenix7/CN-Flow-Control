#include "timer.hpp"

#include <algorithm>

namespace basic_flow {

RttTimer::RttTimer(double initial_timeout_ms)
    : smoothed_rtt_ms_(initial_timeout_ms / 2.0), timeout_ms_(initial_timeout_ms) {}

void RttTimer::on_sample(double rtt_ms) {
  if (!has_sample_) {
    smoothed_rtt_ms_ = rtt_ms;
    has_sample_ = true;
  } else {
    smoothed_rtt_ms_ = (1.0 - kAlpha) * smoothed_rtt_ms_ + kAlpha * rtt_ms;
  }
  timeout_ms_ = std::clamp(2.0 * smoothed_rtt_ms_, kMinTimeoutMs, kMaxTimeoutMs);
}

}  // namespace basic_flow

/*
INITIALIZATION:

smoothed_rtt = initial_timeout / 2
timeout = initial_timeout


WHEN NEW RTT SAMPLE ARRIVES:

if this is the first RTT sample:
  smoothed_rtt = rtt_sample
  mark that a sample has been received

else:
  smoothed_rtt = (1 - alpha) * smoothed_rtt + alpha * rtt_sample

timeout = 2 * smoothed_rtt

if timeout < minimum timeout:
  timeout = minimum timeout

if timeout > maximum timeout:
  timeout = maximum timeout
*/