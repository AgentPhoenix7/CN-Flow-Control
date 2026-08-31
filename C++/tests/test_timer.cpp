#include "timer.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{

using namespace std::chrono_literals;

int test_initial_and_first_sample()
{
  flow_control::TimeoutEstimator estimator;
  if (estimator.timeout() != 100ms) {
    std::cerr << "FAIL: initial timeout\n";
    return 1;
  }

  estimator.observe(100ms, false);
  if (estimator.timeout() != 300ms) {
    std::cerr << "FAIL: first RTT sample\n";
    return 1;
  }

  std::cout << "PASS: initial and first timeout sample\n";
  return 0;
}

int test_ewma_update()
{
  flow_control::TimeoutEstimator estimator;
  estimator.observe(100ms, false);
  estimator.observe(200ms, false);

  if (estimator.timeout() != 363ms) {
    std::cerr << "FAIL: EWMA timeout update\n";
    return 1;
  }

  std::cout << "PASS: EWMA timeout update\n";
  return 0;
}

int test_clamps()
{
  flow_control::TimeoutEstimator low;
  flow_control::TimeoutEstimator high;
  low.observe(1ms, false);
  high.observe(1000ms, false);

  if (low.timeout() != 10ms || high.timeout() != 2000ms) {
    std::cerr << "FAIL: timeout clamps\n";
    return 1;
  }

  std::cout << "PASS: timeout clamps\n";
  return 0;
}

int test_karn_rule_and_invalid_sample()
{
  flow_control::TimeoutEstimator estimator;
  estimator.observe(100ms, false);
  estimator.observe(900ms, true);

  if (estimator.timeout() != 300ms) {
    std::cerr << "FAIL: retransmitted RTT changed timeout\n";
    return 1;
  }

  try {
    estimator.observe(0ms, false);
  } catch (const std::invalid_argument&) {
    if (estimator.timeout() == 300ms) {
      std::cout << "PASS: Karn rule and invalid sample rejection\n";
      return 0;
    }
  }

  std::cerr << "FAIL: invalid RTT sample accepted\n";
  return 1;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += test_initial_and_first_sample();
  failures += test_ewma_update();
  failures += test_clamps();
  failures += test_karn_rule_and_invalid_sample();
  return failures == 0 ? 0 : 1;
}
