#pragma once

#include "olas_protocol.h"

#include <vector>

namespace olas {

// Commanded/overheard state only: there is no RF acknowledgement or telemetry.
struct FanState {
  bool known{false};
  bool on{false};
  uint8_t speed{1};  // Last nonzero speed, retained while off.
};

class Device {
 public:
  Device(uint32_t address, uint8_t check_xor) : address_(address), check_xor_(check_xor) {}

  uint32_t address() const { return address_; }
  uint8_t check_xor() const { return check_xor_; }
  uint8_t sequence() const { return sequence_; }
  const FanState &fan_state() const { return fan_state_; }

  // Pure command construction, NOT a radio send API. A future serialized TX
  // adapter must call this only when accepting a job for this device.
  uint64_t take_command(uint8_t function) {
    const auto raw = encode_frame(address_, make_command(function, sequence_), check_xor_);
    sequence_ = next_sequence(sequence_);
    return raw;
  }

  bool receive(uint64_t raw, uint32_t now_ms) {
    Frame frame;
    if (address_ > MASK_ID || !decode_frame(raw, address_, check_xor_, frame))
      return false;
    // Preserve the prototype's bounded 400 ms heuristic. Copies do not extend
    // the window. A distinct command/sequence is always accepted immediately.
    if (have_last_ && raw == last_frame_ && uint32_t(now_ms - last_seen_ms_) < 400u)
      return false;
    have_last_ = true;
    last_frame_ = raw;
    last_seen_ms_ = now_ms;
    sequence_ = next_sequence(frame.sequence);
    const int speed = speed_for_function(frame.function);
    if (speed >= 0) {
      fan_state_.known = true;
      fan_state_.on = speed != 0;
      if (speed != 0)
        fan_state_.speed = static_cast<uint8_t>(speed);
    }
    return true;
  }

 private:
  uint32_t address_;
  uint8_t check_xor_;
  uint8_t sequence_{1};
  FanState fan_state_;
  uint64_t last_frame_{0};
  uint32_t last_seen_ms_{0};
  bool have_last_{false};
};

class GatewayCore {
 public:
  bool add_device(Device *device) {
    if (device == nullptr || device->address() > MASK_ID || find_device(device->address()) != nullptr)
      return false;
    devices_.push_back(device);
    return true;
  }

  Device *find_device(uint32_t address) const {
    for (auto *device : devices_) {
      if (device->address() == address)
        return device;
    }
    return nullptr;
  }

  bool receive_frame(uint64_t raw, uint32_t now_ms) {
    if (raw > MASK_FRAME || (raw & 1u) != 0u)
      return false;
    auto *device = find_device(static_cast<uint32_t>(raw >> 17));
    return device != nullptr && device->receive(raw, now_ms);
  }

  size_t receive_raw(const int32_t *timings, size_t size, uint32_t now_ms) {
    size_t cursor = 0;
    size_t accepted = 0;
    Frame candidate;
    while (next_raw_candidate(timings, size, cursor, candidate)) {
      if (receive_frame(candidate.raw, now_ms))
        ++accepted;
    }
    return accepted;
  }

 private:
  // Non-owning: ESPHome allocates devices for the firmware's lifetime.
  std::vector<Device *> devices_;
};

}  // namespace olas
