#include "config.hpp"

#include <cstddef>

static_assert(flow_control::MAC_ADDRESS_SIZE == 6U);
static_assert(flow_control::FRAME_HEADER_SIZE == 15U);
static_assert(flow_control::MIN_FRAME_SIZE == 64U);
static_assert(flow_control::MAX_FRAME_SIZE == 1518U);
static_assert(flow_control::CHECKSUM16_SIZE == 2U);
static_assert(flow_control::CRC8_SIZE == 1U);
static_assert(flow_control::CRC10_SIZE == 2U);
static_assert(flow_control::CRC16_SIZE == 2U);
static_assert(flow_control::CRC32_SIZE == 4U);
static_assert(flow_control::MAX_PAYLOAD_SIZE == 1499U);
static_assert(flow_control::DEFAULT_PAYLOAD_SIZE == 46U);

int main()
{
  return 0;
}
