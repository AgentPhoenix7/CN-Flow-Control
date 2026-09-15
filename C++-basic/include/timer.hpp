#pragma once

namespace basic_flow {

/// Simple exponential-moving-average RTT estimator: timeout = 2 * smoothed RTT,
/// clamped to a sane range. This is a basic-scope stand-in for the full
/// Jacobson/Karn algorithm -- it still satisfies the assignment's "compute the most
/// recent RTT and re-compute the timeout" requirement, just without RTTVAR or
/// ambiguous-sample filtering.
class RttTimer {
public:
  explicit RttTimer(double initial_timeout_ms = 300.0);

  /// Feeds one measured round-trip time (start of send to matching ACK) into the
  /// estimator and updates the timeout for the next round.
  void on_sample(double rtt_ms);

  double timeout_ms() const { return timeout_ms_; }

private:
  static constexpr double kMinTimeoutMs = 20.0;
  static constexpr double kMaxTimeoutMs = 5000.0;
  static constexpr double kAlpha = 0.125;

  double smoothed_rtt_ms_;
  double timeout_ms_;
  bool has_sample_ = false;
};

}  // namespace basic_flow
