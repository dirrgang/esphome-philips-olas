#include "olas_core.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <vector>

// Synthetic pairs; check_xor deliberately differs from address-byte XOR.
constexpr uint32_t A = 0x123456, B = 0x654321;
constexpr uint8_t AX = 0xA7, BX = 0x39;

static void append(std::vector<int32_t> &data, uint64_t frame) {
  data.insert(data.end(), {7400, -1090});
  for (int bit = 40; bit >= 0; --bit) {
    if ((frame >> bit) & 1u)
      data.insert(data.end(), {730, -340});
    else
      data.insert(data.end(), {340, -730});
  }
}

int main() {
  olas::GatewayCore gateway;
  olas::Device a(A, AX), b(B, BX), duplicate(A, BX), invalid(0x1000000, AX);
  assert(gateway.add_device(&a) && gateway.add_device(&b));
  assert(!gateway.add_device(&a) && !gateway.add_device(&duplicate));
  assert(!gateway.add_device(&invalid) && !gateway.add_device(nullptr));
  assert(gateway.find_device(A) == &a && gateway.find_device(B) == &b);
  assert(gateway.find_device(0x888888) == nullptr);
  assert(!a.fan_state().known && !b.fan_state().known);

  // Independent command sequences and downward wrap. No radio sends here.
  olas::Frame parsed;
  for (uint8_t seq : {1, 0, 3, 2, 1}) {
    assert(olas::decode_frame(a.take_command(0x40), A, AX, parsed));
    assert(parsed.sequence == seq && parsed.function == 0x40);
    assert(b.sequence() == 1);
  }
  assert(olas::decode_frame(b.take_command(0xAC), B, BX, parsed));
  assert(parsed.sequence == 1);
  assert(olas::decode_frame(a.take_command(0x40), A, AX, parsed));
  assert(parsed.sequence == 0 && b.sequence() == 0);  // A -> B -> A isolation.
  const auto before_a = a.sequence();
  assert(olas::decode_frame(b.take_command(0x10), B, BX, parsed));
  assert(parsed.sequence == 0 && a.sequence() == before_a);

  const auto a_speed6 = olas::encode_frame(A, 0x8C | 2, AX);
  const auto b_speed3 = olas::encode_frame(B, 0x9C | 3, BX);
  assert(gateway.receive_frame(a_speed6, 100));
  assert(a.fan_state().known && a.fan_state().on && a.fan_state().speed == 6);
  assert(!b.fan_state().known && a.sequence() == 1);
  assert(gateway.receive_frame(b_speed3, 101));
  assert(b.fan_state().on && b.fan_state().speed == 3 && b.sequence() == 2);

  // Unknown IDs, wrong per-device checks, and malformed framing change nothing.
  assert(!gateway.receive_frame(olas::encode_frame(A, 0x10, BX), 110));
  assert(!gateway.receive_frame(olas::encode_frame(B, 0x10, AX), 110));
  assert(!gateway.receive_frame(olas::encode_frame(0x888888, 0x10, AX), 110));
  assert(!gateway.receive_frame(a_speed6 | 1u, 110));
  assert(!gateway.receive_frame(a_speed6 | (1ULL << 41), 110));
  assert(a.sequence() == 1 && b.sequence() == 2);
  assert(a.fan_state().speed == 6 && b.fan_state().speed == 3);

  // B does not reset A's filter. A duplicate cannot reset the local sequence.
  a.take_command(0x20);
  assert(a.sequence() == 0);
  assert(!gateway.receive_frame(a_speed6, 120) && a.sequence() == 0);
  assert(!gateway.receive_frame(a_speed6, 499));
  assert(gateway.receive_frame(a_speed6, 500));  // Non-sliding window.
  assert(a.sequence() == 1);
  assert(gateway.receive_frame(olas::encode_frame(A, 0x8C | 1, AX), 501));
  assert(a.sequence() == 0);  // New same-function press with another sequence.
  assert(gateway.receive_frame(olas::encode_frame(A, 0x10, AX), 502));
  assert(!a.fan_state().on && a.fan_state().speed == 6 && a.sequence() == 3);
  assert(b.fan_state().on && b.fan_state().speed == 3);
  assert(gateway.receive_frame(olas::encode_frame(A, 0x64 | 2, AX), 503));
  assert(a.sequence() == 1 && !a.fan_state().on && a.fan_state().speed == 6);

  // Elapsed-time arithmetic across millis() rollover.
  const uint32_t near_wrap = std::numeric_limits<uint32_t>::max() - 50;
  assert(gateway.receive_frame(b_speed3, near_wrap));
  assert(!gateway.receive_frame(b_speed3, 20));
  assert(gateway.receive_frame(b_speed3, 400));

  // One batch: noise, unknown ID, wrong checksum, A x4, B, A-off,
  // malformed bit pulses, truncated tail. Every distinct valid frame dispatches.
  olas::Device c(A, AX), d(B, BX);
  olas::GatewayCore batched;
  assert(batched.add_device(&c) && batched.add_device(&d));
  std::vector<int32_t> timings{7788, -2572, 100, -100};
  append(timings, olas::encode_frame(0x888888, 0x40, AX));
  append(timings, olas::encode_frame(A, 0x40, BX));
  for (int i = 0; i < 4; ++i)
    append(timings, a_speed6);
  append(timings, b_speed3);
  append(timings, olas::encode_frame(A, 0x10, AX));
  append(timings, a_speed6);
  timings[timings.size() - 2] = 9000;
  append(timings, b_speed3);
  timings.resize(timings.size() - 10);
  assert(batched.receive_raw(timings.data(), timings.size(), 0) == 3);
  assert(c.fan_state().known && !c.fan_state().on && c.fan_state().speed == 6);
  assert(d.fan_state().known && d.fan_state().on && d.fan_state().speed == 3);
  assert(c.sequence() == 3 && d.sequence() == 2);
  assert(batched.receive_raw(nullptr, 1000, 0) == 0);
  assert(batched.receive_raw(timings.data(), 0, 0) == 0);
  size_t cursor = timings.size() + 1;
  olas::Frame candidate;
  assert(!olas::next_raw_candidate(timings.data(), timings.size(), cursor, candidate));

  olas::Device low(0, 0), high(0xFFFFFF, 0xFF);
  olas::GatewayCore boundaries;
  assert(boundaries.add_device(&low) && boundaries.add_device(&high));
  assert(boundaries.receive_frame(olas::encode_frame(0, 0x40, 0), 0));
  assert(boundaries.receive_frame(olas::encode_frame(0xFFFFFF, 0x10, 0xFF), 0));
  std::cout << "Olas multi-device core tests passed\n";
}
