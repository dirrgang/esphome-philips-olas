#pragma once

// Multi-device Olas core: per-device sequence, duplicate filter, guards and state
// estimates, plus address routing. Pure C++17 for host tests; no ESPHome headers.

#include "olas_protocol.h"

#include <vector>

namespace olas {

// Commanded or overheard values only: Olas has no RF acknowledgement or telemetry.
struct Estimate {
  bool fan_on{false};
  // Last nonzero speed, retained while off. Starts at the lowest speed as an assumption,
  // so a first turn-on is gentle instead of ESPHome's default of the highest speed.
  uint8_t fan_speed{1};
  bool light_on{false};
  int white_preset{-1};  // -1 until a preset is commanded or heard.
};

class Device {
 public:
  Device(uint32_t address, uint8_t check_xor) : address_(address), check_xor_(check_xor) {}

  uint32_t address() const { return address_; }
  uint8_t check_xor() const { return check_xor_; }
  uint8_t sequence() const { return sequence_; }
  const Estimate &estimate() const { return estimate_; }

  void set_ready(bool ready) { ready_ = ready; }

  // Align with a value restored from flash. This is still an estimate, not an observation.
  void restore_fan_estimate(bool on, uint8_t speed) {
    estimate_.fan_on = on;
    estimate_.fan_speed = speed;
  }
  void restore_white_preset(int preset) { estimate_.white_preset = preset; }

  // Hold while publishing received state: a send from a resulting callback is an echo.
  RfUpdateGuard publication_guard() { return RfUpdateGuard(rf_update_); }

  // True means submitted to the transmitter, NOT delivered. Refused before startup
  // has finished, during publication and for unknown functions.
  template<typename Transmitter> bool send(Transmitter &tx, uint8_t function) {
    if (!send_command(tx, address_, check_xor_, sequence_, function, true, ready_, rf_update_))
      return false;
    apply_(function);
    return true;
  }

  // True for a new valid press of this device. Repeated copies of one press are
  // filtered with the same bounded, non-sliding 400 ms heuristic as the single-device
  // gateway. Another sequence value is a new press and is accepted immediately.
  bool receive(uint64_t raw, uint32_t now_ms, Frame &out) {
    Frame frame;
    if (!decode_frame(raw, address_, check_xor_, frame))
      return false;
    if (have_last_ && raw == last_frame_ && uint32_t(now_ms - last_seen_ms_) < 400u)
      return false;
    have_last_ = true;
    last_frame_ = raw;
    last_seen_ms_ = now_ms;
    sequence_ = next_sequence(frame.sequence);
    apply_(frame.function);
    out = frame;
    return true;
  }

 private:
  void apply_(uint8_t function) {
    const int speed = speed_for_function(function);
    if (speed >= 0) {
      estimate_.fan_on = speed != 0;
      if (speed != 0)
        estimate_.fan_speed = static_cast<uint8_t>(speed);
    } else if (function == SLEEP) {
      estimate_.fan_on = true;  // Observed to start a stopped fan; the speed stays as it was.
    } else if (function == LIGHT_ON || function == LIGHT_OFF) {
      estimate_.light_on = function == LIGHT_ON;
    } else if (white_preset(function) >= 0) {
      estimate_.white_preset = white_preset(function);
    }
  }

  uint32_t address_;
  uint8_t check_xor_;
  uint8_t sequence_{1};
  Estimate estimate_;
  uint64_t last_frame_{0};
  uint32_t last_seen_ms_{0};
  bool have_last_{false};
  bool ready_{false};
  bool rf_update_{false};
};

// Passive learning only: reports structural candidates once per press, for any address.
// A candidate is not a pairing and never changes a device.
class CandidateFilter {
 public:
  template<typename OnCandidate>
  void scan(const int32_t *timings, size_t size, uint32_t now_ms, OnCandidate &&on_candidate) {
    size_t cursor = 0;
    Frame candidate;
    while (next_raw_candidate(timings, size, cursor, candidate)) {
      if (have_last_ && candidate.raw == last_frame_ && uint32_t(now_ms - last_seen_ms_) < 400u)
        continue;
      have_last_ = true;
      last_frame_ = candidate.raw;
      last_seen_ms_ = now_ms;
      on_candidate(candidate);
    }
  }

 private:
  uint64_t last_frame_{0};
  uint32_t last_seen_ms_{0};
  bool have_last_{false};
};

class GatewayCore {
 public:
  bool add_device(Device *device) {
    if (device == nullptr || device->address() > MASK_ID || find_device(device->address()) != nullptr)
      return false;
    device->set_ready(ready_);
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

  size_t device_count() const { return devices_.size(); }

  // Olas sends stay blocked until the firmware has finished starting.
  void set_ready(bool ready) {
    ready_ = ready;
    for (auto *device : devices_)
      device->set_ready(ready);
  }

  // Visits every complete frame in a raw batch. Frames for unknown addresses and frames
  // with a wrong check are ignored. on_accepted(device, frame) runs under that device's
  // publication guard. Returns the number of accepted presses.
  template<typename OnAccepted>
  size_t receive_raw(const int32_t *timings, size_t size, uint32_t now_ms, OnAccepted &&on_accepted) {
    size_t cursor = 0;
    size_t accepted = 0;
    Frame candidate;
    while (next_raw_candidate(timings, size, cursor, candidate)) {
      auto *device = find_device(candidate.fan_id);
      Frame frame;
      if (device == nullptr || !device->receive(candidate.raw, now_ms, frame))
        continue;
      const auto guard = device->publication_guard();
      on_accepted(*device, frame);
      ++accepted;
    }
    return accepted;
  }

 private:
  // Non-owning: ESPHome allocates devices for the firmware's lifetime.
  std::vector<Device *> devices_;
  bool ready_{false};
};

}  // namespace olas
