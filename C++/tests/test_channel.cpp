#include "channel.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{

int test_forced_outcomes()
{
  const std::vector<std::uint8_t> data{0x00U, 0xFFU};
  flow_control::Channel clean{{0.0, 0.0, 0.0, 1U, 2U}};
  flow_control::Channel dropped{{1.0, 0.0, 0.0, 1U, 2U}};
  flow_control::Channel delayed{{0.0, 1.0, 0.0, 1U, 2U}};
  flow_control::Channel corrupted{{0.0, 0.0, 1.0, 1U, 2U}};

  const auto clean_result = clean.transmit(data);
  const auto drop_result = dropped.transmit(data);
  const auto delay_result = delayed.transmit(data);
  const auto corrupt_result = corrupted.transmit(data);

  if (clean_result.outcome != flow_control::ChannelOutcome::Clean
      || clean_result.bytes != data
      || drop_result.outcome != flow_control::ChannelOutcome::Dropped
      || !drop_result.bytes.empty()
      || delay_result.outcome != flow_control::ChannelOutcome::Delayed
      || !delay_result.bytes.empty()
      || corrupt_result.outcome != flow_control::ChannelOutcome::Corrupted
      || corrupt_result.bytes == data) {
    std::cerr << "FAIL: forced channel outcomes\n";
    return 1;
  }

  std::cout << "PASS: forced channel outcomes\n";
  return 0;
}

int test_reproducible_corruption()
{
  flow_control::Channel first{{0.0, 0.0, 1.0, 7U, 11U}};
  flow_control::Channel second{{0.0, 0.0, 1.0, 7U, 11U}};
  const std::vector<std::uint8_t> data{0x00U, 0x00U};
  const auto first_result = first.transmit(data);
  const auto second_result = second.transmit(data);

  if (first_result.bytes != second_result.bytes) {
    std::cerr << "FAIL: reproducible channel corruption\n";
    return 1;
  }

  std::cout << "PASS: reproducible channel corruption\n";
  return 0;
}

int test_empty_corruption_is_dropped()
{
  flow_control::Channel channel{{0.0, 0.0, 1.0, 1U, 2U}};
  const auto result = channel.transmit({});

  if (result.outcome != flow_control::ChannelOutcome::Dropped
      || !result.bytes.empty()) {
    std::cerr << "FAIL: empty corruption handling\n";
    return 1;
  }

  std::cout << "PASS: empty corruption handling\n";
  return 0;
}

int test_invalid_probabilities()
{
  const std::vector<flow_control::ChannelConfig> invalid{
    {-0.1, 0.0, 0.0, 1U, 2U},
    {0.0, 1.1, 0.0, 1U, 2U},
    {0.0, 0.0, 2.0, 1U, 2U}
  };

  for (const auto config : invalid) {
    try {
      (void)flow_control::Channel{config};
    } catch (const std::invalid_argument&) {
      continue;
    }
    std::cerr << "FAIL: invalid channel probability accepted\n";
    return 1;
  }

  std::cout << "PASS: invalid channel probability rejection\n";
  return 0;
}

}  // namespace

int main()
{
  int failures = 0;
  failures += test_forced_outcomes();
  failures += test_reproducible_corruption();
  failures += test_empty_corruption_is_dropped();
  failures += test_invalid_probabilities();
  return failures == 0 ? 0 : 1;
}
