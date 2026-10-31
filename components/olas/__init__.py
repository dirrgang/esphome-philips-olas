"""Receive-only multi-device Olas core; native entities and RF TX follow later."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import remote_base
from esphome.const import CONF_ADDRESS, CONF_ID

DEPENDENCIES = ["remote_receiver", "remote_transmitter"]
CONF_DEVICES = "devices"
CONF_CHECK_XOR = "check_xor"
CONF_RECEIVER_ID = "receiver_id"
CONF_TRANSMITTER_ID = "transmitter_id"

# Keep distinct from the portable ::olas codec namespace: ESPHome's generated
# main.cpp uses `using namespace esphome`.
olas_ns = cg.esphome_ns.namespace("olas_component")
OlasGateway = olas_ns.class_("OlasGateway", cg.Component, remote_base.RemoteReceiverListener)
# The pure core deliberately lives outside esphome::, without framework headers.
OlasDevice = cg.global_ns.namespace("olas").class_("Device")


def _integer(value):
    # Python bool is an int subclass; cv.int_range alone accepts YAML true/false.
    if isinstance(value, (bool, float)):
        raise cv.Invalid("Expected an integer, not a boolean or float")
    return cv.int_(value)


def _unique_addresses(devices):
    seen = set()
    for index, device in enumerate(devices):
        address = device[CONF_ADDRESS]
        if address in seen:
            raise cv.Invalid("Duplicate Olas address", path=[index, CONF_ADDRESS])
        seen.add(address)
    return devices


DEVICE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.declare_id(OlasDevice),
        cv.Required(CONF_ADDRESS): cv.All(_integer, cv.int_range(min=0, max=0xFFFFFF)),
        cv.Required(CONF_CHECK_XOR): cv.All(_integer, cv.int_range(min=0, max=0xFF)),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OlasGateway),
        cv.Required(CONF_RECEIVER_ID): cv.use_id(remote_base.RemoteReceiverBase),
        cv.Required(CONF_TRANSMITTER_ID): cv.use_id(remote_base.RemoteTransmitterBase),
        cv.Required(CONF_DEVICES): cv.All(
            cv.ensure_list(DEVICE_SCHEMA), cv.Length(min=1), _unique_addresses
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    gateway = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(gateway, config)
    # Counts the listener slot required by ESPHome 2026.10.1's StaticVector.
    await remote_base.register_listener(gateway, config)
    transmitter = await cg.get_variable(config[CONF_TRANSMITTER_ID])
    cg.add(gateway.set_transmitter(transmitter))
    for device in config[CONF_DEVICES]:
        var = cg.new_Pvariable(
            device[CONF_ID], device[CONF_ADDRESS], device[CONF_CHECK_XOR]
        )
        cg.add(gateway.add_device(var))
