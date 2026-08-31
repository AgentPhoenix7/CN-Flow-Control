/**
 * @file timer.hpp
 * @brief Declares adaptive retransmission timeout estimation.
 */

#ifndef FLOW_CONTROL_TIMER_HPP
#define FLOW_CONTROL_TIMER_HPP

#include <chrono>

namespace flow_control
{

/** RFC-style SRTT/RTTVAR estimator with Karn's retransmission rule. */
class TimeoutEstimator
{
public:
  /** Initializes the timeout to 100 milliseconds. */
  TimeoutEstimator() noexcept;

  /** Returns the current timeout clamped to 10--2000 milliseconds. */
  std::chrono::milliseconds timeout() const noexcept;

  /**
   * @brief Observes a positive RTT sample unless it was retransmitted.
   * @throws std::invalid_argument for a nonpositive sample.
   */
  void observe(
    std::chrono::milliseconds rtt,
    bool retransmitted
  );

private:
  bool initialized_;
  double srtt_ms_;
  double rttvar_ms_;
  std::chrono::milliseconds timeout_;

  void recompute_timeout() noexcept;
};

}  // namespace flow_control

#endif
