#include "olas.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::olas_component {

static const char *const TAG = "olas";

void OlasGateway::add_device(::olas::Device *device) {
  if (!this->core_.add_device(device)) {
    ESP_LOGE(TAG, "Invalid or duplicate logical device");
    this->mark_failed();
  }
}

bool OlasGateway::on_receive(remote_base::RemoteReceiveData data) {
  const auto &raw = data.get_raw_data();
  return this->core_.receive_raw(raw.data(), raw.size(), millis()) != 0;
}

void OlasGateway::dump_config() {
  ESP_LOGCONFIG(TAG, "Olas gateway core (receive only; no HA entities or RF TX)");
}

}  // namespace esphome::olas_component
