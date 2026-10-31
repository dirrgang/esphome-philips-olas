#pragma once

#include "olas_core.h"
#include "esphome/core/component.h"
#include "esphome/components/remote_base/remote_base.h"

namespace esphome::olas_component {

class OlasGateway : public Component, public remote_base::RemoteReceiverListener {
 public:
  void add_device(::olas::Device *device);
  // Reserved typed transport binding. No sends or CC1101 mode changes yet.
  void set_transmitter(remote_base::RemoteTransmitterBase *transmitter) { transmitter_ = transmitter; }
  bool on_receive(remote_base::RemoteReceiveData data) override;
  void dump_config() override;

 protected:
  ::olas::GatewayCore core_;
  remote_base::RemoteTransmitterBase *transmitter_{nullptr};
};

}  // namespace esphome::olas_component
