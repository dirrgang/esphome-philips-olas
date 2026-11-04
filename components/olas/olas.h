#pragma once

#include "olas_core.h"
#include "esphome/components/button/button.h"
#include "esphome/components/event/event.h"
#include "esphome/components/fan/fan.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/select/select.h"
#include "esphome/core/component.h"

#include <vector>

namespace esphome::olas_component {

class OlasGateway;
class OlasFan;

// One logical Olas address with its entities. All RF requests pass through send().
class OlasDevice {
 public:
  OlasDevice(const char *name, uint32_t address, uint8_t check_xor) : name_(name), core_(address, check_xor) {}

  void set_gateway(OlasGateway *gateway) { this->gateway_ = gateway; }
  void set_fan(OlasFan *fan) { this->fan_ = fan; }
  void set_light(light::LightState *light) { this->light_ = light; }
  void set_white_select(select::Select *white_select) { this->white_select_ = white_select; }
  void set_action_event(event::Event *action_event) { this->action_event_ = action_event; }

  ::olas::Device &core() { return this->core_; }
  const char *name() const { return this->name_; }

  // True means submitted, not delivered. Refused before startup has finished and
  // while received state is being published.
  bool send(uint8_t function);
  // Runs under the core's publication guard.
  void on_frame(const ::olas::Frame &frame);

 protected:
  void publish_fan_();
  void publish_white_(uint8_t function);

  const char *name_;
  ::olas::Device core_;
  OlasGateway *gateway_{nullptr};
  OlasFan *fan_{nullptr};
  light::LightState *light_{nullptr};
  select::Select *white_select_{nullptr};
  event::Event *action_event_{nullptr};
};

// Never claims received data, so the RF proxy and other listeners on the radio keep working.
class OlasGateway : public Component, public remote_base::RemoteReceiverListener {
 public:
  void add_device(OlasDevice *device);
  void set_transmitter(remote_base::RemoteTransmitterBase *transmitter) { this->transmitter_ = transmitter; }
  remote_base::RemoteTransmitterBase *transmitter() { return this->transmitter_; }

  void start_learning();
  void stop_learning();

  void setup() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;
  void dump_config() override;
  // Same point as the single-device gateway, which unlocks sends at boot priority -100.
  float get_setup_priority() const override { return setup_priority::LATE; }

 protected:
  ::olas::GatewayCore core_;
  ::olas::CandidateFilter learn_filter_;
  std::vector<OlasDevice *> devices_;
  remote_base::RemoteTransmitterBase *transmitter_{nullptr};
  bool learning_{false};
};

// A received update never reaches control(), so publishing cannot echo RF.
class OlasFan : public fan::Fan, public Component {
 public:
  explicit OlasFan(OlasDevice *device) : device_(device) {}
  void setup() override;
  void dump_config() override;
  fan::FanTraits get_traits() override;

 protected:
  void control(const fan::FanCall &call) override;
  OlasDevice *device_;
};

// LightState writes outputs later from loop(), after any publication guard has ended,
// so the output stays inert. RF belongs only to the synchronous remote-values listener.
class OlasLightOutput : public light::LightOutput, public light::LightRemoteValuesListener {
 public:
  explicit OlasLightOutput(OlasDevice *device) : device_(device) {}
  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override;
  void write_state(light::LightState *state) override {}
  void on_light_remote_values_update() override;

 protected:
  OlasDevice *device_;
  light::LightState *state_{nullptr};
};

class OlasWhiteSelect : public select::Select {
 public:
  explicit OlasWhiteSelect(OlasDevice *device) : device_(device) {}

 protected:
  void control(size_t index) override;
  OlasDevice *device_;
};

class OlasActionButton : public button::Button {
 public:
  OlasActionButton(OlasDevice *device, uint8_t function) : device_(device), function_(function) {}

 protected:
  void press_action() override { this->device_->send(this->function_); }
  OlasDevice *device_;
  uint8_t function_;
};

class OlasLearnButton : public button::Button {
 public:
  OlasLearnButton(OlasGateway *gateway, bool start) : gateway_(gateway), start_(start) {}

 protected:
  void press_action() override;
  OlasGateway *gateway_;
  bool start_;
};

class OlasActionEvent : public event::Event {};

}  // namespace esphome::olas_component
