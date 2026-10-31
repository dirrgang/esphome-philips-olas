#include <olas_protocol.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

// Synthetic fixtures, independent of any installed device or private capture.
static constexpr uint32_t ID = 0x654321u;
static constexpr uint8_t CHECK_XOR = 0xA7u;

static std::vector<int32_t> wave(uint64_t raw) {
  std::vector<int32_t> v;
  v.push_back(7400);
  v.push_back(-1090);
  for (int bit = 40; bit >= 0; --bit) {
    if ((raw >> bit) & 1u) {
      v.push_back(730);
      v.push_back(-340);
    } else {
      v.push_back(340);
      v.push_back(-730);
    }
  }
  return v;
}

int main() {
  static_assert(olas::next_sequence(0) == 3);
  static_assert(olas::next_sequence(3) == 2);
  static_assert(olas::checksum(0x40, CHECK_XOR) == 0xE7);
  static_assert(olas::encode_frame(ID, 0x40, CHECK_XOR) == 0xCA864281CEULL);

  const uint8_t funcs[7] = {0x10, 0x40, 0xAC, 0x9C, 0x20, 0x80, 0x8C};
  for (int speed = 0; speed <= 6; speed++) {
    assert(olas::function_for_speed(speed) == funcs[speed]);
    assert(olas::speed_for_function(funcs[speed]) == speed);
    for (unsigned seq = 0; seq < 4; ++seq) {
      auto cmd = olas::make_command(funcs[speed], seq);
      auto raw = olas::encode_frame(ID, cmd, CHECK_XOR);
      olas::Frame parsed;
      assert(olas::decode_frame(raw, ID, CHECK_XOR, parsed));
      assert(parsed.fan_id == ID);
      assert(parsed.function == funcs[speed]);
      assert(parsed.sequence == seq);
      assert(parsed.check == olas::checksum(cmd, CHECK_XOR));
      assert(!olas::decode_frame(raw, ID + 1, CHECK_XOR, parsed));
      assert(!olas::decode_frame(raw, ID, CHECK_XOR ^ 1, parsed));
    }
  }
  assert(olas::speed_for_function(0x64) == -1);  // neutral white
  olas::Frame parsed;
  assert(!olas::decode_frame((1ULL << 41), ID, CHECK_XOR, parsed));
  assert(!olas::decode_frame(olas::encode_frame(ID, 0x40, CHECK_XOR) | 1ULL, ID,
                             CHECK_XOR, parsed));

  // Prepend noise and repeat three frames to mimic ESPHome's batched capture.
  auto data = std::vector<int32_t>{7788, -2572};
  auto single = wave(olas::encode_frame(ID, 0xAC | 1u, CHECK_XOR));
  for (int i = 0; i < 3; ++i)
    data.insert(data.end(), single.begin(), single.end());
  assert(olas::decode_raw(data.data(), data.size(), ID, CHECK_XOR, parsed));
  assert(parsed.function == 0xAC);
  assert(parsed.sequence == 1);
  assert(!olas::decode_raw(data.data(), data.size(), ID, 0x5B, parsed));
  auto truncated = wave(olas::encode_frame(ID, 0x40, CHECK_XOR));
  truncated.resize(70);
  assert(!olas::decode_raw(truncated.data(), truncated.size(), ID, CHECK_XOR, parsed));

  // Discovery must accept unfamiliar IDs/check rules without calling them valid.
  // Two different devices in one RX batch must both remain visible.
  constexpr uint32_t OTHER_ID = 0x123456u;
  constexpr uint8_t OTHER_XOR = 0xD7u;  // deliberately not the XOR of ID bytes
  auto discovery = std::vector<int32_t>{7400, -3000, 123, -456};
  auto foreign = wave(olas::encode_frame(OTHER_ID, 0x71, OTHER_XOR));
  discovery.insert(discovery.end(), foreign.begin(), foreign.end());
  discovery.insert(discovery.end(), single.begin(), single.end());
  size_t cursor = 0;
  assert(olas::next_raw_candidate(discovery.data(), discovery.size(), cursor, parsed));
  assert(parsed.fan_id == OTHER_ID && parsed.function == 0x70 && parsed.sequence == 1);
  assert(olas::candidate_check_xor(parsed) == OTHER_XOR);
  assert(!olas::decode_frame(parsed.raw, OTHER_ID, CHECK_XOR, parsed));
  assert(olas::next_raw_candidate(discovery.data(), discovery.size(), cursor, parsed));
  assert(parsed.fan_id == ID && olas::candidate_check_xor(parsed) == CHECK_XOR);
  assert(!olas::next_raw_candidate(discovery.data(), discovery.size(), cursor, parsed));
  assert(!olas::next_raw_candidate(discovery.data(), discovery.size(), cursor, parsed));
  assert(olas::decode_raw(discovery.data(), discovery.size(), ID, CHECK_XOR, parsed));
  assert(parsed.fan_id == ID);  // normal RX skips an unrelated candidate

  // Structural candidates still require complete timings and a zero tail bit.
  auto bad_tail = wave(olas::encode_frame(OTHER_ID, 0x71, OTHER_XOR) | 1ULL);
  auto bad_pulse = foreign;
  bad_pulse[20] = 1000;
  cursor = 0;
  assert(!olas::next_raw_candidate(bad_tail.data(), bad_tail.size(), cursor, parsed));
  cursor = 0;
  assert(!olas::next_raw_candidate(bad_pulse.data(), bad_pulse.size(), cursor, parsed));
  cursor = 0;
  assert(!olas::next_raw_candidate(truncated.data(), truncated.size(), cursor, parsed));
  assert(!olas::next_raw_candidate(nullptr, 84, cursor, parsed));
  cursor = discovery.size() + 1;
  assert(!olas::next_raw_candidate(discovery.data(), discovery.size(), cursor, parsed));

  // The inferred constant must stay the same across commands/sequences for a
  // synthetic device; real devices still need multiple independent captures.
  for (auto function : funcs) {
    for (uint8_t seq = 0; seq < 4; ++seq) {
      assert(olas::unpack_candidate(olas::encode_frame(
          OTHER_ID, olas::make_command(function, seq), OTHER_XOR), parsed));
      assert(olas::candidate_check_xor(parsed) == OTHER_XOR);
    }
  }

  std::cout << "Olas protocol tests passed\n";
}
