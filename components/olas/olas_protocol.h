#pragma once

// Philips Olas 41-bit ASK/OOK frame codec.
// Pure C++17 for deterministic host-side tests; no ESPHome/Arduino dependencies.
// Observed device-specific check XOR constant is provided by the caller.

#include <cstddef>
#include <cstdint>

namespace olas {

constexpr uint32_t MASK_ID = 0xFFFFFFu;
constexpr uint64_t MASK_FRAME = (uint64_t{1} << 41) - 1u;
constexpr int BITS_PER_FRAME = 41;

constexpr uint8_t next_sequence(uint8_t sequence) {
  return static_cast<uint8_t>((sequence + 3u) & 0x03u);
}

constexpr uint8_t function_code(uint8_t command) { return command & 0xFCu; }
constexpr uint8_t sequence_number(uint8_t command) { return command & 0x03u; }
constexpr uint8_t make_command(uint8_t function, uint8_t sequence) {
  return static_cast<uint8_t>((function & 0xFCu) | (sequence & 0x03u));
}
constexpr uint8_t checksum(uint8_t command, uint8_t check_xor) {
  return static_cast<uint8_t>(command ^ check_xor);
}
constexpr uint64_t encode_frame(uint32_t fan_id, uint8_t command, uint8_t check_xor) {
  return (static_cast<uint64_t>(fan_id & MASK_ID) << 17) |
         (static_cast<uint64_t>(command) << 9) |
         (static_cast<uint64_t>(checksum(command, check_xor)) << 1);
}

struct Frame {
  uint32_t fan_id{0};
  uint8_t command{0};
  uint8_t function{0};
  uint8_t sequence{0};
  uint8_t check{0};
  uint64_t raw{0};
};

constexpr bool decode_frame(uint64_t raw, uint32_t expected_id, uint8_t check_xor,
                            Frame &out) {
  if (raw > MASK_FRAME || (raw & 1u) != 0u)
    return false;
  const auto id = static_cast<uint32_t>((raw >> 17) & MASK_ID);
  const auto command = static_cast<uint8_t>((raw >> 9) & 0xFFu);
  const auto check = static_cast<uint8_t>((raw >> 1) & 0xFFu);
  if (id != (expected_id & MASK_ID) || check != checksum(command, check_xor))
    return false;
  out = {id, command, function_code(command), sequence_number(command), check, raw};
  return true;
}

// The Olas receiver has no need to recognize every function in order to validate
// a frame. The command-to-action mapping is deliberately separate.
constexpr int speed_for_function(uint8_t function) {
  switch (function) {
    case 0x10: return 0;
    case 0x40: return 1;
    case 0xAC: return 2;
    case 0x9C: return 3;
    case 0x20: return 4;
    case 0x80: return 5;
    case 0x8C: return 6;
    default: return -1;
  }
}

constexpr uint8_t function_for_speed(int speed) {
  constexpr uint8_t codes[7] = {0x10, 0x40, 0xAC, 0x9C, 0x20, 0x80, 0x8C};
  return speed >= 0 && speed <= 6 ? codes[speed] : 0u;
}

// Find one structurally valid frame, advancing cursor past it. Address and
// checksum policy belong to the caller. Each candidate's pulses are parsed once,
// regardless of the number of configured devices.
inline bool next_raw_frame(const int32_t *timings, size_t size, size_t &cursor,
                           uint64_t &out) {
  if (timings == nullptr || size < 84 || cursor > size - 84)
    return false;
  for (size_t start = cursor; start <= size - 84; ++start) {
    const auto sync_mark = timings[start];
    const auto sync_space = timings[start + 1];
    if (sync_mark < 6500 || sync_mark > 8200 || sync_space < -1450 || sync_space > -750)
      continue;
    uint64_t raw = 0;
    bool valid = true;
    for (int bit = 0; bit < BITS_PER_FRAME; ++bit) {
      const auto mark = timings[start + 2 + static_cast<size_t>(bit) * 2];
      const auto space = timings[start + 3 + static_cast<size_t>(bit) * 2];
      const bool one = mark >= 600 && mark <= 850 && space >= -450 && space <= -250;
      const bool zero = mark >= 250 && mark <= 450 && space >= -900 && space <= -600;
      if (!one && !zero) {
        valid = false;
        break;
      }
      raw = (raw << 1) | static_cast<uint64_t>(one);
    }
    if (valid && (raw & 1u) == 0u) {
      cursor = start + 84;
      out = raw;
      return true;
    }
  }
  cursor = size;
  return false;
}

// Compatibility API: return the first frame matching one device in a batch.
inline bool decode_raw(const int32_t *timings, size_t size, uint32_t expected_id,
                       uint8_t check_xor, Frame &out) {
  size_t cursor = 0;
  uint64_t raw;
  while (next_raw_frame(timings, size, cursor, raw)) {
    if (decode_frame(raw, expected_id, check_xor, out))
      return true;
  }
  return false;
}

}  // namespace olas
