#include "olas_core.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

// Synthetic pairs; check_xor deliberately differs from the address-byte XOR.
constexpr uint32_t A = 0x123456, B = 0x654321;
constexpr uint8_t AX = 0xA7, BX = 0x39;

struct FakeTransmitter {
  std::vector<std::pair<uint32_t, uint32_t>> items;
  int performed{0};
  uint32_t send_times{0};

  struct Data {
    FakeTransmitter *parent;
    void item(uint32_t mark, uint32_t space) { parent->items.emplace_back(mark, space); }
  };
  struct Call {
    FakeTransmitter *parent;
    Data data;
    Data *get_data() { return &data; }
    void set_send_times(uint32_t times) { parent->send_times = times; }
    void set_send_wait(uint32_t) {}
    void perform() { ++parent->performed; }
  };
  Call transmit() {
    items.clear();
    return Call{this, Data{this}};
  }
  // Last transmission as raw timings, so it can be fed back through the decoder.
  std::vector<int32_t> timings() const {
    std::vector<int32_t> out;
    for (const auto &item : items) {
      out.push_back(static_cast<int32_t>(item.first));
      out.push_back(-static_cast<int32_t>(item.second));
    }
    return out;
  }
};

static void append(std::vector<int32_t> &data, uint64_t frame) {
  data.insert(data.end(), {7400, -1090});
  for (int bit = 40; bit >= 0; --bit) {
    if ((frame >> bit) & 1u)
      data.insert(data.end(), {730, -340});
    else
      data.insert(data.end(), {340, -730});
  }
}

static std::vector<int32_t> batch(std::initializer_list<uint64_t> frames) {
  std::vector<int32_t> data;
  for (auto frame : frames)
    append(data, frame);
  return data;
}

static size_t feed(olas::GatewayCore &gateway, const std::vector<int32_t> &data, uint32_t now) {
  return gateway.receive_raw(data.data(), data.size(), now,
                             [](olas::Device &, const olas::Frame &) {});
}

static void test_registration() {
  olas::GatewayCore gateway;
  olas::Device a(A, AX), b(B, BX), duplicate(A, BX), invalid(0x1000000, AX);
  assert(gateway.add_device(&a) && gateway.add_device(&b));
  assert(!gateway.add_device(&a) && !gateway.add_device(&duplicate));
  assert(!gateway.add_device(&invalid) && !gateway.add_device(nullptr));
  assert(gateway.device_count() == 2);
  assert(gateway.find_device(A) == &a && gateway.find_device(B) == &b);
  assert(gateway.find_device(0x888888) == nullptr);

  olas::Device low(0, 0), high(0xFFFFFF, 0xFF);
  olas::GatewayCore boundaries;
  assert(boundaries.add_device(&low) && boundaries.add_device(&high));
  assert(feed(boundaries, batch({olas::encode_frame(0, 0x40, 0)}), 0) == 1);
  assert(feed(boundaries, batch({olas::encode_frame(0xFFFFFF, 0x10, 0xFF)}), 0) == 1);
}

static void test_send_gates_and_sequences() {
  olas::GatewayCore gateway;
  olas::Device a(A, AX), b(B, BX);
  assert(gateway.add_device(&a) && gateway.add_device(&b));
  FakeTransmitter tx;

  // Startup: no RF, no sequence change and no estimate change before ready.
  assert(!a.send(tx, 0x40) && tx.performed == 0 && a.sequence() == 1);
  assert(!a.estimate().fan_on);
  gateway.set_ready(true);

  // Unknown functions are refused.
  assert(!a.send(tx, 0x04) && tx.performed == 0 && a.sequence() == 1);

  // Independent downward wrap, A -> B -> A, with the waveform of each own address.
  olas::Frame parsed;
  for (uint8_t seq : {1, 0, 3, 2, 1}) {
    assert(a.send(tx, 0x40));
    assert(tx.send_times == 4 && tx.items.size() == 42);
    auto data = tx.timings();
    assert(olas::decode_raw(data.data(), data.size(), A, AX, parsed));
    assert(parsed.sequence == seq && parsed.function == 0x40);
    assert(b.sequence() == 1);
  }
  const auto a_next = a.sequence();
  assert(b.send(tx, 0xAC));
  auto data = tx.timings();
  assert(olas::decode_raw(data.data(), data.size(), B, BX, parsed));
  assert(parsed.sequence == 1 && parsed.function == 0xAC);
  assert(a.sequence() == a_next && b.sequence() == 0);
  assert(!olas::decode_raw(data.data(), data.size(), A, AX, parsed));
}

static void test_receive_routing_and_isolation() {
  olas::GatewayCore gateway;
  olas::Device a(A, AX), b(B, BX);
  assert(gateway.add_device(&a) && gateway.add_device(&b));

  const auto a_speed6 = olas::encode_frame(A, 0x8C | 2, AX);
  const auto b_speed3 = olas::encode_frame(B, 0x9C | 3, BX);
  assert(feed(gateway, batch({a_speed6}), 100) == 1);
  assert(a.estimate().fan_on && a.estimate().fan_speed == 6 && a.sequence() == 1);
  assert(!b.estimate().fan_on && b.estimate().fan_speed == 1 && b.sequence() == 1);
  assert(feed(gateway, batch({b_speed3}), 101) == 1);
  assert(b.estimate().fan_on && b.estimate().fan_speed == 3 && b.sequence() == 2);

  // Unknown IDs and wrong per-device checks change nothing.
  assert(feed(gateway, batch({olas::encode_frame(A, 0x10, BX)}), 110) == 0);
  assert(feed(gateway, batch({olas::encode_frame(B, 0x10, AX)}), 110) == 0);
  assert(feed(gateway, batch({olas::encode_frame(0x888888, 0x10, AX)}), 110) == 0);
  assert(a.sequence() == 1 && b.sequence() == 2);
  assert(a.estimate().fan_on && b.estimate().fan_on);
}

static void test_duplicate_filter() {
  olas::GatewayCore gateway;
  olas::Device a(A, AX), b(B, BX);
  assert(gateway.add_device(&a) && gateway.add_device(&b));
  gateway.set_ready(true);
  FakeTransmitter tx;
  const auto frame = olas::encode_frame(A, 0x8C | 2, AX);
  assert(feed(gateway, batch({frame}), 0) == 1);

  // A copy cannot rewind the sequence after a local send, and B leaves A's filter alone.
  assert(a.send(tx, 0x20) && a.sequence() == 0);
  assert(feed(gateway, batch({olas::encode_frame(B, 0x40, BX)}), 10) == 1);
  assert(feed(gateway, batch({frame}), 120) == 0 && a.sequence() == 0);
  assert(feed(gateway, batch({frame}), 399) == 0);
  assert(feed(gateway, batch({frame}), 400) == 1);  // Non-sliding window.
  assert(a.sequence() == 1);

  // The same function with another sequence is a new, legitimate press.
  assert(feed(gateway, batch({olas::encode_frame(A, 0x8C | 1, AX)}), 401) == 1);
  assert(a.sequence() == 0);

  // Elapsed-time arithmetic across millis() rollover.
  const auto b_frame = olas::encode_frame(B, 0x9C | 3, BX);
  const uint32_t near_wrap = std::numeric_limits<uint32_t>::max() - 50;
  assert(feed(gateway, batch({b_frame}), near_wrap) == 1);
  assert(feed(gateway, batch({b_frame}), 20) == 0);
  assert(feed(gateway, batch({b_frame}), 400) == 1);
}

static void test_batch_parsing() {
  olas::GatewayCore gateway;
  olas::Device a(A, AX), b(B, BX);
  assert(gateway.add_device(&a) && gateway.add_device(&b));
  const auto a_speed6 = olas::encode_frame(A, 0x8C | 2, AX);
  const auto b_speed3 = olas::encode_frame(B, 0x9C | 3, BX);

  // Noise, unknown ID, wrong check, A x4, B, A off, a malformed frame, a truncated tail.
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

  std::vector<std::pair<uint32_t, uint8_t>> seen;
  const auto accepted = gateway.receive_raw(
      timings.data(), timings.size(), 0, [&](olas::Device &device, const olas::Frame &frame) {
        seen.emplace_back(device.address(), frame.function);
      });
  assert(accepted == 3);
  assert((seen == std::vector<std::pair<uint32_t, uint8_t>>{{A, 0x8C}, {B, 0x9C}, {A, 0x10}}));
  assert(!a.estimate().fan_on && a.estimate().fan_speed == 6);
  assert(b.estimate().fan_on && b.estimate().fan_speed == 3);
  assert(a.sequence() == 3 && b.sequence() == 2);

  auto ignored = [](olas::Device &, const olas::Frame &) { assert(false); };
  assert(gateway.receive_raw(nullptr, 1000, 0, ignored) == 0);
  assert(gateway.receive_raw(timings.data(), 0, 0, ignored) == 0);
}

static void test_no_echo_while_publishing() {
  olas::GatewayCore gateway;
  olas::Device a(A, AX), b(B, BX);
  assert(gateway.add_device(&a) && gateway.add_device(&b));
  gateway.set_ready(true);
  FakeTransmitter tx;

  bool called = false;
  const auto data = batch({olas::encode_frame(A, 0x40 | 2, AX)});
  gateway.receive_raw(data.data(), data.size(), 0, [&](olas::Device &device, const olas::Frame &) {
    called = true;
    assert(!device.send(tx, 0x10));  // Publication of received state must not transmit.
    assert(&device == &a && b.send(tx, 0x10));  // The guard is per device.
  });
  assert(called && tx.performed == 1);
  assert(a.send(tx, 0x10));  // The guard ends with the callback.
}

static void test_estimates() {
  olas::GatewayCore gateway;
  olas::Device a(A, AX), b(B, BX);
  assert(gateway.add_device(&a) && gateway.add_device(&b));
  gateway.set_ready(true);
  FakeTransmitter tx;

  // Initial values are assumptions, not observations.
  assert(!a.estimate().fan_on && a.estimate().fan_speed == 1);
  assert(!a.estimate().light_on && a.estimate().white_preset == -1);

  // Sleep turns the estimate ON and keeps the assumed starting speed.
  assert(a.send(tx, olas::SLEEP));
  assert(a.estimate().fan_on && a.estimate().fan_speed == 1);

  // Commanded values and retained speed.
  assert(a.send(tx, olas::function_for_speed(4)) && a.estimate().fan_speed == 4);
  assert(a.send(tx, olas::function_for_speed(0)));
  assert(!a.estimate().fan_on && a.estimate().fan_speed == 4);

  // Light power and white preset are independent of each other and of the fan.
  assert(a.send(tx, olas::LIGHT_ON) && a.estimate().light_on && !a.estimate().fan_on);
  assert(a.send(tx, olas::WHITE_PRESETS[1]) && a.estimate().white_preset == 1);
  assert(a.estimate().light_on);
  assert(feed(gateway, batch({olas::encode_frame(A, olas::LIGHT_OFF | 3, AX)}), 0) == 1);
  assert(!a.estimate().light_on && a.estimate().white_preset == 1);

  // Stateless actions do not touch any estimate.
  for (uint8_t action : {olas::DIRECTION_TOGGLE, olas::TIMER_1H, olas::TIMER_3H, olas::TIMER_6H}) {
    assert(a.send(tx, action));
    assert(!a.estimate().fan_on && a.estimate().fan_speed == 4);
  }

  // Device B never saw any of it.
  assert(!b.estimate().fan_on && !b.estimate().light_on && b.estimate().white_preset == -1);
}

static void test_restored_estimate() {
  olas::GatewayCore gateway;
  olas::Device a(A, AX);
  assert(gateway.add_device(&a));
  gateway.set_ready(true);
  FakeTransmitter tx;

  // A restored value sends nothing, and sleep keeps the restored speed.
  a.restore_fan_estimate(false, 5);
  assert(tx.performed == 0 && a.sequence() == 1);
  assert(a.send(tx, olas::SLEEP));
  assert(a.estimate().fan_on && a.estimate().fan_speed == 5);

  // A restored ON and preset send nothing either, and the next command replaces them.
  olas::Device b(B, BX);
  assert(gateway.add_device(&b));
  const auto sent = tx.performed;
  b.restore_fan_estimate(true, 3);
  b.restore_white_preset(2);
  assert(tx.performed == sent && b.sequence() == 1);
  assert(b.estimate().fan_on && b.estimate().fan_speed == 3 && b.estimate().white_preset == 2);
  assert(b.send(tx, olas::function_for_speed(0)) && !b.estimate().fan_on);
  assert(b.estimate().fan_speed == 3 && b.estimate().white_preset == 2);

  // A restored sequence is used by the next command, sends nothing by itself and
  // stays independent per device. Out-of-range saved values cannot leave two bits.
  olas::Device c(0x777777, 0x22);
  assert(gateway.add_device(&c));
  const auto before = tx.performed;
  const auto a_sequence = a.sequence();
  c.restore_sequence(3);
  assert(tx.performed == before && c.sequence() == 3 && a.sequence() == a_sequence);
  assert(c.send(tx, olas::LIGHT_OFF));
  auto data = tx.timings();
  olas::Frame parsed;
  assert(olas::decode_raw(data.data(), data.size(), 0x777777, 0x22, parsed));
  assert(parsed.sequence == 3 && c.sequence() == 2);
  c.restore_sequence(0xFF);
  assert(c.sequence() == 3);

  // A received press still overrides the restored value.
  assert(feed(gateway, batch({olas::encode_frame(0x777777, olas::LIGHT_ON | 1, 0x22)}), 0) == 1);
  assert(c.sequence() == 0);
}

static void test_learning_candidates() {
  olas::GatewayCore gateway;
  olas::Device a(A, AX);
  assert(gateway.add_device(&a));
  olas::CandidateFilter filter;

  // Unknown address and repeated copies: one report per press, and no device changes.
  const auto unknown = olas::encode_frame(0x888888, 0x40 | 2, 0x11);
  const auto data = batch({unknown, unknown, unknown, unknown});
  std::vector<olas::Frame> seen;
  auto collect = [&](const olas::Frame &frame) { seen.push_back(frame); };
  filter.scan(data.data(), data.size(), 0, collect);
  assert(seen.size() == 1);
  assert(seen[0].fan_id == 0x888888 && seen[0].function == 0x40 && seen[0].sequence == 2);
  assert(olas::candidate_check_xor(seen[0]) == 0x11);
  filter.scan(data.data(), data.size(), 399, collect);
  assert(seen.size() == 1);
  filter.scan(data.data(), data.size(), 400, collect);
  assert(seen.size() == 2);

  // Another sequence value is another press.
  const auto next = batch({olas::encode_frame(0x888888, 0x40 | 1, 0x11)});
  filter.scan(next.data(), next.size(), 401, collect);
  assert(seen.size() == 3);
  assert(a.sequence() == 1 && !a.estimate().fan_on);
}

int main() {
  test_restored_estimate();
  test_learning_candidates();
  test_registration();
  test_send_gates_and_sequences();
  test_receive_routing_and_isolation();
  test_duplicate_filter();
  test_batch_parsing();
  test_no_echo_while_publishing();
  test_estimates();
  std::cout << "Olas multi-device core tests passed\n";
}
