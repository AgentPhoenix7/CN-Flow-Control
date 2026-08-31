#include "error_injection.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{

int test_msb_indexed_bit_flip()
{
  std::vector<std::uint8_t> data{0x00U, 0x00U};
  const bool first = flow_control::flip_bit(data, 0U);
  const bool eighth = flow_control::flip_bit(data, 7U);
  const bool ninth = flow_control::flip_bit(data, 8U);

  if (!first || !eighth || !ninth || data != std::vector<std::uint8_t>{0x81U, 0x80U}) {
    std::cerr << "FAIL: MSB-indexed bit flips\n";
    return 1;
  }

  std::cout << "PASS: MSB-indexed bit flips\n";
  return 0;
}

int test_burst_and_atomic_rejection()
{
  std::vector<std::uint8_t> data{0x00U, 0x00U};

  if (!flow_control::flip_burst(data, 6U, 4U)
      || data != std::vector<std::uint8_t>{0x03U, 0xC0U}) {
    std::cerr << "FAIL: cross-byte burst\n";
    return 1;
  }

  const auto before = data;
  if (flow_control::flip_burst(data, 14U, 3U) || data != before) {
    std::cerr << "FAIL: invalid burst modified data\n";
    return 1;
  }

  std::cout << "PASS: burst mutation and atomic rejection\n";
  return 0;
}

int test_reproducible_rng()
{
  flow_control::ErrorInjectionRng first{1U};
  flow_control::ErrorInjectionRng second{1U};

  if (first.next() != 1015568748U
      || second.next() != 1015568748U
      || first.next() != second.next()) {
    std::cerr << "FAIL: reproducible RNG sequence\n";
    return 1;
  }

  std::cout << "PASS: reproducible RNG sequence\n";
  return 0;
}

int test_probability_selection()
{
  flow_control::ErrorInjectionRng rng{1U};
  const auto initial_state = rng.state();

  if (flow_control::select_probability(rng, 0.0)
      || !flow_control::select_probability(rng, 1.0)
      || rng.state() != initial_state
      || !flow_control::select_probability(rng, 0.5)) {
    std::cerr << "FAIL: probability selection\n";
    return 1;
  }

  const auto state_before_invalid = rng.state();
  try {
    (void)flow_control::select_probability(rng, 1.1);
  } catch (const std::invalid_argument&) {
    if (rng.state() == state_before_invalid) {
      std::cout << "PASS: probability selection\n";
      return 0;
    }
  }

  std::cerr << "FAIL: invalid probability changed RNG\n";
  return 1;
}

int test_independent_rng_state()
{
  flow_control::ErrorInjectionRng selection_rng{1U};
  flow_control::ErrorInjectionRng bit_rng{1U};
  flow_control::ErrorInjectionRng control_bit_rng{1U};
  std::vector<std::uint8_t> actual{0x00U};
  std::vector<std::uint8_t> expected{0x00U};

  (void)flow_control::select_probability(selection_rng, 0.5);
  const bool actual_ok = flow_control::flip_random_bit(actual, bit_rng);
  const bool expected_ok = flow_control::flip_random_bit(
    expected, control_bit_rng
  );

  if (!actual_ok || !expected_ok || actual != expected || actual != std::vector<std::uint8_t>{0x08U}) {
    std::cerr << "FAIL: independent RNG state\n";
    return 1;
  }

  std::cout << "PASS: independent RNG state\n";
  return 0;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += test_msb_indexed_bit_flip();
  failures += test_burst_and_atomic_rejection();
  failures += test_reproducible_rng();
  failures += test_probability_selection();
  failures += test_independent_rng_state();
  return failures == 0 ? 0 : 1;
}
