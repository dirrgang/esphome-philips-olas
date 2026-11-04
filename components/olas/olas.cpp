#include "olas.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::olas_component {

static const char *const TAG = "olas";
static const char *const TAG_LEARN = "olas.learn";
static constexpr uint32_t LEARN_WINDOW_MS = 60000;
static constexpr int SPEED_COUNT = 6;

bool OlasDevice::send(uint8_t function) {
  const uint8_t sequence = this->core_.sequence();
  if (!this->core_.send(*this->gateway_->transmitter(), function))
    return false;
  // RF IDs stay out of the log.
  ESP_LOGI(TAG, "%s: TX requested function=0x%02X seq=%u", this->name_, unsigned(function), unsigned(sequence));

  const auto guard = this->core_.publication_guard();
  if (::olas::speed_for_function(function) >= 0 || function == ::olas::SLEEP)
    this->publish_fan_();
  this->publish_white_(function);
  if (const char *type = ::olas::action_event_type(function, false))
    this->action_event_->trigger(type);
  return true;
}

void OlasDevice::on_frame(const ::olas::Frame &frame) {
  ESP_LOGI(TAG, "%s: RX function=0x%02X seq=%u", this->name_, unsigned(frame.function), unsigned(frame.sequence));
  const auto function = frame.function;
  if (::olas::speed_for_function(function) >= 0 || function == ::olas::SLEEP) {
    this->publish_fan_();
  } else if (function == ::olas::LIGHT_ON || function == ::olas::LIGHT_OFF) {
    const bool on = this->core_.estimate().light_on;
    this->light_->remote_values.set_state(on);
    this->light_->current_values.set_state(on);
    this->light_->publish_state();
  }
  this->publish_white_(function);
  // An action never presses a button or implies a mode state.
  if (const char *type = ::olas::action_event_type(function, true))
    this->action_event_->trigger(type);
}

void OlasDevice::publish_fan_() {
  const auto &estimate = this->core_.estimate();
  this->fan_->state = estimate.fan_on;
  this->fan_->speed = estimate.fan_speed;
  this->fan_->publish_state();
}

void OlasDevice::publish_white_(uint8_t function) {
  const int preset = ::olas::white_preset(function);
  if (preset >= 0)
    this->white_select_->publish_state(static_cast<size_t>(preset));
}

void OlasGateway::add_device(OlasDevice *device) {
  // Its entities exist either way, and a rejected device stays not-ready, so it never sends.
  device->set_gateway(this);
  // The schema already rejects these; this guards direct C++ use.
  if (!this->core_.add_device(&device->core())) {
    ESP_LOGE(TAG, "Invalid or duplicate logical device");
    this->mark_failed();
    return;
  }
  this->devices_.push_back(device);
}

void OlasGateway::setup() { this->core_.set_ready(true); }

void OlasGateway::start_learning() {
  this->learning_ = true;
  ESP_LOGI(TAG_LEARN, "Learning for 60 s; press only the intended remote. Candidates are unverified.");
  this->set_timeout("learn", LEARN_WINDOW_MS, [this]() {
    this->learning_ = false;
    ESP_LOGI(TAG_LEARN, "Learning window closed.");
  });
}

void OlasGateway::stop_learning() {
  this->cancel_timeout("learn");
  this->learning_ = false;
  ESP_LOGI(TAG_LEARN, "Learning stopped.");
}

bool OlasGateway::on_receive(remote_base::RemoteReceiveData data) {
  const auto &raw = data.get_raw_data();
  const uint32_t now = millis();
  if (this->learning_) {
    this->learn_filter_.scan(raw.data(), raw.size(), now, [](const ::olas::Frame &candidate) {
      ESP_LOGI(TAG_LEARN, "RF candidate id=0x%06X function=0x%02X seq=%u check=0x%02X xor_candidate=0x%02X",
               unsigned(candidate.fan_id), unsigned(candidate.function), unsigned(candidate.sequence),
               unsigned(candidate.check), unsigned(::olas::candidate_check_xor(candidate)));
    });
  }
  this->core_.receive_raw(raw.data(), raw.size(), now, [this](::olas::Device &core, const ::olas::Frame &frame) {
    for (auto *device : this->devices_) {
      if (&device->core() == &core) {
        device->on_frame(frame);
        return;
      }
    }
  });
  return false;  // Never consume: the proxy and other listeners share this receiver.
}

void OlasGateway::dump_config() {
  ESP_LOGCONFIG(TAG, "Olas gateway: %u logical device(s)", unsigned(this->devices_.size()));
}

void OlasFan::setup() {
  auto restore = this->restore_state_();
  if (restore.has_value())
    restore->apply(*this);  // Publishes only: control() is the sole RF path.
  this->device_->core().restore_fan_estimate(this->state, static_cast<uint8_t>(this->speed));
}

void OlasFan::dump_config() { LOG_FAN("", "Olas Fan", this); }

fan::FanTraits OlasFan::get_traits() { return fan::FanTraits(false, true, false, SPEED_COUNT); }

void OlasFan::control(const fan::FanCall &call) {
  const bool next_state = call.get_state().value_or(this->state);
  int next_speed = call.get_speed().value_or(this->speed);
  if (next_state && next_speed == 0)
    next_speed = 1;
  // On success send() publishes the new estimate. On refusal, return HA to the current one.
  if (!this->device_->send(::olas::function_for_speed(next_state ? next_speed : 0)))
    this->publish_state();
}

light::LightTraits OlasLightOutput::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::ON_OFF});
  return traits;
}

void OlasLightOutput::setup_state(light::LightState *state) {
  this->state_ = state;
  state->add_remote_values_listener(this);
}

void OlasLightOutput::on_light_remote_values_update() {
  // The core refuses this before startup has finished and during received publication.
  this->device_->send(this->state_->remote_values.is_on() ? ::olas::LIGHT_ON : ::olas::LIGHT_OFF);
}

void OlasWhiteSelect::control(size_t index) {
  if (index < 3)
    this->device_->send(::olas::WHITE_PRESETS[index]);
}

void OlasLearnButton::press_action() {
  if (this->start_) {
    this->gateway_->start_learning();
  } else {
    this->gateway_->stop_learning();
  }
}

}  // namespace esphome::olas_component
