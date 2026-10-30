#include <olas_protocol.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

static constexpr uint32_t ID = 0xAA8C03u;
static constexpr uint8_t CHECK_XOR = 0x25u;

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
  static_assert(olas::checksum(0x40, CHECK_XOR) == 0x65);
  static_assert(olas::encode_frame(ID, 0x40, CHECK_XOR) == 0x155180680CAULL);

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

  std::cout << "Olas protocol tests passed\n";
}
