#include <olas_protocol.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

// Synthetic fixtures, independent of any installed device or private capture.
static constexpr uint32_t ID = 0x654321u;
static constexpr uint8_t CHECK_XOR = 0xA7u;

// Capture the exact shared sender's ESPHome calls without transmitting anything.
struct FakeTransmitter {
  std::vector<int32_t> timings;
  std::vector<std::vector<int32_t>> frames;
  unsigned calls{0}, repeats{0}, wait{999};
  void item(uint32_t mark, uint32_t space) {
    timings.push_back(static_cast<int32_t>(mark));
    timings.push_back(-static_cast<int32_t>(space));
  }
  struct Call {
    FakeTransmitter &tx;
    FakeTransmitter *get_data() { return &tx; }
    void set_send_times(unsigned n) { tx.repeats = n; }
    void set_send_wait(unsigned n) { tx.wait = n; }
    void perform() {
      ++tx.calls;
      for (unsigned i = 0; i < tx.repeats; ++i)
        tx.frames.push_back(tx.timings);
    }
  };
  Call transmit() { timings.clear(); return {*this}; }
};

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
  // Fan, light and stateless actions all use the same waveform and counter.
  FakeTransmitter tx;
  uint8_t sequence = 1;
  const uint8_t actions[] = {0x40, 0x7C, 0xAC, 0xBC, 0x9C, 0x6C,
                             0x20, 0x64, 0x80, 0x84, 0x8C, 0x10,
                             0x50, 0x30, 0x1C, 0x18, 0x14};
  for (uint8_t function : actions) {
    for (unsigned i = 0; i < 4; ++i) {
      const uint8_t before = sequence;
      const auto copies_before = tx.frames.size();
      assert(olas::send_command(tx, ID, CHECK_XOR, sequence, function, true, true, false));
      assert(sequence == ((before + 3u) & 3u));
      assert(tx.repeats == 4 && tx.wait == 0 && tx.timings.size() == 84);
      for (size_t copy = copies_before; copy < tx.frames.size(); ++copy)
        assert(tx.frames[copy] == tx.timings);
      olas::Frame sent;
      assert(olas::decode_raw(tx.timings.data(), tx.timings.size(), ID, CHECK_XOR, sent));
      assert(sent.function == function && sent.sequence == before);
      assert(sent.check == ((function | before) ^ CHECK_XOR));
      // Independently unpack MSB-first pulses; do not rely only on codec roundtrip.
      uint64_t raw = 0;
      assert(tx.timings[0] == 7400 && tx.timings[1] == -1090);
      for (size_t bit = 0; bit < 41; ++bit) {
        const auto mark = tx.timings[2 + bit * 2];
        const bool one = mark == 730;
        assert(mark == (one ? 730 : 340));
        assert(tx.timings[3 + bit * 2] == (one ? -340 : -730));
        raw = (raw << 1) | one;
      }
      assert(raw == ((uint64_t(ID) << 17) | (uint64_t(function | before) << 9) |
                     (uint64_t((function | before) ^ CHECK_XOR) << 1)));
      assert(!olas::decode_frame(raw ^ 2u, ID, CHECK_XOR, sent));
      assert(!olas::decode_frame(raw, ID + 1, CHECK_XOR, sent));
      assert(!olas::decode_frame(raw, ID | 0x1000000u, CHECK_XOR, sent));
    }
  }
  assert(olas::white_preset(0x6C) == 0);
  assert(olas::white_preset(0x64) == 1);
  assert(olas::white_preset(0x84) == 2);
  assert(olas::white_preset(0x7C) == -1);

  const unsigned calls_before = tx.calls;
  const uint8_t seq_before = sequence;
  assert(!olas::send_command(tx, ID, CHECK_XOR, sequence, 0x7C, false, true, false));
  assert(!olas::send_command(tx, ID, CHECK_XOR, sequence, 0x7C, true, false, false));
  assert(!olas::send_command(tx, ID | 0x1000000u, CHECK_XOR, sequence, 0x7C, true, true, false));
  assert(!olas::send_command(tx, ID, CHECK_XOR, sequence, 0x70, true, true, false));
  assert(!olas::send_command(tx, ID, CHECK_XOR, sequence, 0x28, true, true, false));
  assert(tx.calls == calls_before && sequence == seq_before);
  bool rf_update = false;
  for (auto function : actions) {
    // Model a validated remote press and synchronous entity on_state callback.
    sequence = olas::next_sequence(2);
    {
      olas::RfUpdateGuard guard(rf_update);
      assert(!olas::send_command(tx, ID, CHECK_XOR, sequence, function, true, true, rf_update));
      { olas::RfUpdateGuard nested(rf_update); }
      assert(rf_update);
    }
    assert(!rf_update && tx.calls == calls_before && sequence == 1);
  }
  // New press after RX (including the same function) is legitimate, never latched out.
  for (int i = 0; i < 2; ++i)
    assert(olas::send_command(tx, ID, CHECK_XOR, sequence, 0x7C, true, true, rf_update));
  assert(tx.calls == calls_before + 2 && sequence == 3);

  // Interleaved fan -> direction -> light -> sleep -> timers share one counter.
  sequence = 1;
  const uint8_t mixed[] = {0xAC, 0x50, 0x7C, 0x30, 0x1C, 0x18, 0x14, 0x8C};
  const uint8_t expected_sequences[] = {1, 0, 3, 2, 1, 0, 3, 2};
  for (size_t i = 0; i < sizeof(mixed); ++i) {
    assert(olas::send_command(tx, ID, CHECK_XOR, sequence, mixed[i], true, true, false));
    olas::Frame sent;
    assert(olas::decode_raw(tx.timings.data(), tx.timings.size(), ID, CHECK_XOR, sent));
    assert(sent.function == mixed[i] && sent.sequence == expected_sequences[i]);
  }

  const uint8_t stateless[] = {olas::DIRECTION_TOGGLE, olas::SLEEP,
                              olas::TIMER_1H, olas::TIMER_3H, olas::TIMER_6H};
  const char *requested[] = {"direction_requested", "sleep_requested", "timer_1h_requested",
                            "timer_3h_requested", "timer_6h_requested"};
  const char *received[] = {"direction_received", "sleep_received", "timer_1h_received",
                           "timer_3h_received", "timer_6h_received"};
  for (size_t i = 0; i < sizeof(stateless); ++i) {
    assert(stateless[i] == actions[12 + i]);  // Explicit captured-code expectations above.
    assert(olas::is_stateless_action(stateless[i]));
    assert(std::strcmp(olas::action_event_type(stateless[i], false), requested[i]) == 0);
    assert(std::strcmp(olas::action_event_type(stateless[i], true), received[i]) == 0);
    // RX mapping cannot claim fan power/speed, light power or a white preset.
    assert(olas::speed_for_function(stateless[i]) == -1);
    assert(olas::white_preset(stateless[i]) == -1);
    assert(stateless[i] != olas::LIGHT_ON && stateless[i] != olas::LIGHT_OFF);
    const auto before = tx.calls;
    const auto seq = sequence;
    assert(!olas::send_command(tx, ID, CHECK_XOR, sequence, stateless[i], false, true, false));
    assert(!olas::send_command(tx, ID, CHECK_XOR, sequence, stateless[i], true, false, false));
    assert(tx.calls == before && sequence == seq);
    // Each deliberate repeat is a new logical action, including direction toggle.
    for (int press = 0; press < 2; ++press)
      assert(olas::send_command(tx, ID, CHECK_XOR, sequence, stateless[i], true, true, false));
    assert(tx.calls == before + 2 && sequence == ((seq + 2u) & 3u));
  }
  for (uint8_t function : {uint8_t(0x40), uint8_t(0x7C), uint8_t(0x64), uint8_t(0x70), uint8_t(0x00)}) {
    assert(!olas::is_stateless_action(function));
    assert(olas::action_event_type(function, false) == nullptr);
    assert(olas::action_event_type(function, true) == nullptr);
  }

  // Event callbacks are synchronous too. A received-action notification must
  // never turn into TX or consume a second sequence, including nested callbacks.
  for (auto function : stateless) {
    const auto before = tx.calls;
    sequence = olas::next_sequence(3);
    {
      olas::RfUpdateGuard guard(rf_update);
      assert(olas::action_event_type(function, true) != nullptr);
      assert(!olas::send_command(tx, ID, CHECK_XOR, sequence, function, true, true, rf_update));
    }
    assert(tx.calls == before && sequence == 2 && !rf_update);
  }

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
