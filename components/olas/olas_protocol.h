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

// Structural parsing only: an unknown device's check rule is not validated.
constexpr bool unpack_candidate(uint64_t raw, Frame &out) {
  if (raw > MASK_FRAME || (raw & 1u) != 0u)
    return false;
  const auto id = static_cast<uint32_t>((raw >> 17) & MASK_ID);
  const auto command = static_cast<uint8_t>((raw >> 9) & 0xFFu);
  const auto check = static_cast<uint8_t>((raw >> 1) & 0xFFu);
  out = {id, command, function_code(command), sequence_number(command), check, raw};
  return true;
}

constexpr uint8_t candidate_check_xor(const Frame &frame) {
  return static_cast<uint8_t>(frame.command ^ frame.check);
}

constexpr bool decode_frame(uint64_t raw, uint32_t expected_id, uint8_t check_xor,
                            Frame &out) {
  Frame candidate;
  if (expected_id > MASK_ID || !unpack_candidate(raw, candidate) ||
      candidate.fan_id != (expected_id & MASK_ID) ||
      candidate.check != checksum(candidate.command, check_xor))
    return false;
  out = candidate;
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

// A single raw receiver batch can contain multiple copies of a 41-bit frame.
// Marks are positive, spaces are negative, in microseconds. This intentionally
// scans complete structural candidates, without validating any device/check.
// Start cursor at zero; repeated calls visit every candidate in the batch.
inline bool next_raw_candidate(const int32_t *timings, size_t size, size_t &cursor,
                               Frame &out) {
  constexpr size_t FRAME_PULSES = 84;
  if (timings == nullptr || size < FRAME_PULSES || cursor > size)
    return false;

  for (size_t start = cursor; start <= size - FRAME_PULSES; ++start) {
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
    if (valid && unpack_candidate(raw, out)) {
      cursor = start + FRAME_PULSES;
      return true;
    }
  }
  cursor = size;
  return false;
}

// Normal control still requires the configured device and check XOR; merely
// looking like an Olas frame is insufficient. Return the first matching frame.
inline bool decode_raw(const int32_t *timings, size_t size, uint32_t expected_id,
                       uint8_t check_xor, Frame &out) {
  size_t cursor = 0;
  Frame candidate;
  while (next_raw_candidate(timings, size, cursor, candidate)) {
    if (decode_frame(candidate.raw, expected_id, check_xor, out))
      return true;
  }
  return false;
}

constexpr uint8_t LIGHT_ON = 0x7C;
constexpr uint8_t LIGHT_OFF = 0xBC;
constexpr uint8_t WHITE_PRESETS[] = {0x6C, 0x64, 0x84};
constexpr uint8_t DIRECTION_TOGGLE = 0x50;
constexpr uint8_t SLEEP = 0x30;
constexpr uint8_t TIMER_1H = 0x1C;
constexpr uint8_t TIMER_3H = 0x18;
constexpr uint8_t TIMER_6H = 0x14;

// Actions have no reconciled absolute state or remaining-time telemetry.
constexpr bool is_stateless_action(uint8_t function) {
  return function == DIRECTION_TOGGLE || function == SLEEP ||
         function == TIMER_1H || function == TIMER_3H || function == TIMER_6H;
}

// A request/observation is an event, not evidence of an active mode or timer.
constexpr const char *action_event_type(uint8_t function, bool received) {
  switch (function) {
    case DIRECTION_TOGGLE: return received ? "direction_received" : "direction_requested";
    case SLEEP: return received ? "sleep_received" : "sleep_requested";
    case TIMER_1H: return received ? "timer_1h_received" : "timer_1h_requested";
    case TIMER_3H: return received ? "timer_3h_received" : "timer_3h_requested";
    case TIMER_6H: return received ? "timer_6h_received" : "timer_6h_requested";
    default: return nullptr;
  }
}

constexpr int white_preset(uint8_t function) {
  for (int i = 0; i < 3; ++i)
    if (WHITE_PRESETS[i] == function)
      return i;
  return -1;
}

// Use around synchronous entity publication; callbacks must never echo RX.
class RfUpdateGuard {
 public:
  explicit RfUpdateGuard(bool &flag) : flag_(flag), previous_(flag) { flag_ = true; }
  ~RfUpdateGuard() { flag_ = previous_; }
  RfUpdateGuard(const RfUpdateGuard &) = delete;
  RfUpdateGuard &operator=(const RfUpdateGuard &) = delete;
 private:
  bool &flag_;
  bool previous_;
};

// Sleep is observed to start a stopped fan. Publish commanded/overheard power
// without inventing a speed, active-mode state or a second RF command.
template<typename Fan>
void publish_action_power(uint8_t function, Fan &fan, bool &rf_update) {
  if (function != SLEEP)
    return;
  RfUpdateGuard guard(rf_update);
  fan.state = true;
  fan.publish_state();
}

// Duck-typed ESPHome TransmitCall API, also exercised by a host fake. No radio
// ownership, queue or framework dependency. True means submitted, NOT delivered.
template<typename Transmitter>
bool send_command(Transmitter &tx, uint32_t address, uint8_t check_xor,
                  uint8_t &sequence, uint8_t function, bool enabled,
                  bool ready, bool rf_update) {
  if (!enabled || !ready || rf_update || address > MASK_ID ||
      (speed_for_function(function) < 0 && function != LIGHT_ON &&
       function != LIGHT_OFF && white_preset(function) < 0 &&
       !is_stateless_action(function)))
    return false;
  const auto raw = encode_frame(address, make_command(function, sequence), check_xor);
  auto call = tx.transmit();
  auto *data = call.get_data();
  data->item(7400, 1090);
  for (int bit = BITS_PER_FRAME - 1; bit >= 0; --bit) {
    const bool one = (raw >> bit) & 1u;
    data->item(one ? 730 : 340, one ? 340 : 730);
  }
  call.set_send_times(4);
  call.set_send_wait(0);
  call.perform();
  sequence = next_sequence(sequence);
  return true;
}

}  // namespace olas
