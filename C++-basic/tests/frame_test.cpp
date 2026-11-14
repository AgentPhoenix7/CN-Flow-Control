#include "frame.hpp"
#include "crc32.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}

int main() {
  using namespace basic_flow;
  try {
    require(crc32_compute({}) == 0x00000000u, "CRC-32 empty-input vector failed");
    require(crc32_compute({'1','2','3','4','5','6','7','8','9'}) == 0xCBF43926u,
            "CRC-32 check vector failed");
    require(crc32_compute({'a','b','c'}) == 0x352441C2u, "CRC-32 abc vector failed");
    std::vector<std::uint8_t> binary;
    for (int i = 0; i < 256; ++i) binary.push_back(static_cast<std::uint8_t>(i));
    require(crc32_compute(binary) == 0x29058C73u, "CRC-32 binary vector failed");
    DataFrame frame;
    frame.src_mac = {2, 0, 0, 0, 0, 1};
    frame.dst_mac = {2, 0, 0, 0, 0, 2};
    frame.length = 3;
    frame.seq = 127;
    frame.payload[0] = 'a';
    frame.payload[1] = 'b';
    frame.payload[2] = 'c';
    const auto body = serialize(frame);
    require(body.size() == 65, "DATA body must have a four-byte FCS (65 bytes total)");
    // Independent fixture: Python zlib.crc32 over this 61-byte header/payload.
    const std::vector<std::uint8_t> trailer{0x23, 0x40, 0x06, 0x38};
    require(std::equal(trailer.begin(), trailer.end(), body.begin() + 61),
            "FCS must match CRC-32 in network byte order");
    DataFrame decoded;
    require(deserialize_and_verify(body, decoded), "valid CRC-32 frame rejected");
    require(decoded.src_mac == frame.src_mac && decoded.dst_mac == frame.dst_mac &&
            decoded.length == frame.length && decoded.seq == frame.seq &&
            decoded.payload == frame.payload, "frame fields did not round trip");

    for (std::size_t bit = 0; bit < body.size() * 8; ++bit) {
      auto corrupt = body;
      corrupt[bit / 8] ^= static_cast<std::uint8_t>(1u << (bit % 8));
      require(!deserialize_and_verify(corrupt, decoded),
              "single-bit corruption in header, payload or FCS was accepted");
    }
    for (std::size_t size = 0; size < body.size(); ++size) {
      require(!deserialize_and_verify({body.begin(), body.begin() + size}, decoded),
              "truncated frame was accepted");
    }
    auto extended = body;
    extended.push_back(0);
    require(!deserialize_and_verify(extended, decoded), "oversized frame was accepted");
    frame.length = 47;
    require(!deserialize_and_verify(serialize(frame), decoded),
            "invalid payload length with valid CRC was accepted");
    frame.length = 0x0100;
    require(!deserialize_and_verify(serialize(frame), decoded),
            "high length byte was ignored");

    for (const std::size_t size : {0u, 1u, 45u, 46u, 47u, 92u, 11823u}) {
      std::vector<std::uint8_t> input(size);
      for (std::size_t i = 0; i < size; ++i) input[i] = static_cast<std::uint8_t>(i);
      const auto frames = split_into_frames(input);
      require(frames.size() == (size + 45) / 46, "incorrect file split count");
      std::vector<std::uint8_t> reconstructed;
      for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto encoded = serialize(frames[i]);
        require(encoded.size() == 65 && deserialize_and_verify(encoded, decoded),
                "split frame failed CRC-32 verification");
        require(decoded.seq == static_cast<std::uint8_t>(i), "sequence wrap failed");
        require(std::all_of(decoded.payload.begin() + decoded.length, decoded.payload.end(),
                            [](std::uint8_t b) { return b == 0; }), "nonzero payload padding");
        reconstructed.insert(reconstructed.end(), decoded.payload.begin(),
                             decoded.payload.begin() + decoded.length);
      }
      require(reconstructed == input, "split/serialize/verify corrupted file data");
    }
    std::cout << "CRC-32 vectors, golden frame, 520 bit flips, malformed frames, padding and wraparound passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
