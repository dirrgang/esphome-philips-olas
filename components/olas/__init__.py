"""Multi-device Olas component on a shared receiver and transmitter (ADR 0001)."""

import esphome.codegen as cg
from esphome.components import button, event, fan, light, remote_base, select
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_NAME,
    CONF_OUTPUT_ID,
    CONF_RESTORE_MODE,
)

AUTO_LOAD = ["button", "event", "fan", "light", "select"]
DEPENDENCIES = ["remote_receiver", "remote_transmitter"]
CODEOWNERS = ["@dirrgang"]

CONF_ACTION_EVENT = "action_event"
CONF_CHECK_XOR = "check_xor"
CONF_DEVICES = "devices"
CONF_FAN = "fan"
CONF_LEARN_START = "learn_start"
CONF_LEARN_STOP = "learn_stop"
CONF_LIGHT = "light"
CONF_RECEIVER_ID = "receiver_id"
CONF_TRANSMITTER_ID = "transmitter_id"
CONF_WHITE_PRESET = "white_preset"

# Distinct from the portable ::olas codec namespace, because ESPHome's generated
# main.cpp uses `using namespace esphome`.
olas_ns = cg.esphome_ns.namespace("olas_component")
OlasGateway = olas_ns.class_(
    "OlasGateway", cg.Component, remote_base.RemoteReceiverListener
)
OlasDevice = olas_ns.class_("OlasDevice")
OlasFan = olas_ns.class_("OlasFan", fan.Fan, cg.Component)
OlasLightOutput = olas_ns.class_("OlasLightOutput", light.LightOutput)
OlasWhiteSelect = olas_ns.class_("OlasWhiteSelect", select.Select)
OlasActionButton = olas_ns.class_("OlasActionButton", button.Button)
OlasLearnButton = olas_ns.class_("OlasLearnButton", button.Button)
OlasActionEvent = olas_ns.class_("OlasActionEvent", event.Event)

# Config key, default name suffix and codec constant. The suffixes equal the entity
# names of the single-device gateway, so a migrated fan keeps its Home Assistant identity.
ACTIONS = [
    ("direction", "Drehrichtung wechseln", "DIRECTION_TOGGLE"),
    ("sleep", "Schlafmodus auslösen", "SLEEP"),
    ("timer_1h", "Timer 1 h", "TIMER_1H"),
    ("timer_3h", "Timer 3 h", "TIMER_3H"),
    ("timer_6h", "Timer 6 h", "TIMER_6H"),
]
WHITE_OPTIONS = ["Warmweiß", "Neutralweiß", "Tageslichtweiß"]
EVENT_TYPES = [
    "direction_requested",
    "direction_received",
    "sleep_requested",
    "sleep_received",
    "timer_1h_requested",
    "timer_1h_received",
    "timer_3h_requested",
    "timer_3h_received",
    "timer_6h_requested",
    "timer_6h_received",
]


def _integer(value):
    # bool is an int subclass, so cv.int_range alone would accept YAML true/false.
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


def _fill_entity_names(device):
    """Derive entity names from the device name; an entity block may override them."""
    if not isinstance(device, dict) or CONF_NAME not in device:
        return device  # The schema reports the missing or malformed value.
    name = device[CONF_NAME]
    filled = dict(device)

    def fill(key, entity_name, **extra):
        entity = dict(device.get(key) or {})
        entity.setdefault(CONF_NAME, entity_name)
        for extra_key, value in extra.items():
            entity.setdefault(extra_key, value)
        filled[key] = entity

    fill(CONF_FAN, name)
    # The light starts as an assumed OFF; it is not restored as if it were measured.
    fill(CONF_LIGHT, f"{name} Licht", restore_mode="ALWAYS_OFF")
    fill(CONF_WHITE_PRESET, f"{name} Weißton")
    fill(CONF_ACTION_EVENT, f"{name} Aktion")
    for key, suffix, _ in ACTIONS:
        fill(key, f"{name} {suffix}")
    return filled


DEVICE_SCHEMA = cv.All(
    _fill_entity_names,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.declare_id(OlasDevice),
            cv.Required(CONF_NAME): cv.string_strict,
            cv.Required(CONF_ADDRESS): cv.All(
                _integer, cv.int_range(min=0, max=0xFFFFFF)
            ),
            # Explicit per device: the ID-byte XOR is a candidate, not a universal rule.
            cv.Required(CONF_CHECK_XOR): cv.All(
                _integer, cv.int_range(min=0, max=0xFF)
            ),
            cv.Required(CONF_FAN): fan.fan_schema(OlasFan).extend(cv.COMPONENT_SCHEMA),
            cv.Required(CONF_LIGHT): light.BINARY_LIGHT_SCHEMA.extend(
                {cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(OlasLightOutput)}
            ),
            cv.Required(CONF_WHITE_PRESET): select.select_schema(OlasWhiteSelect),
            cv.Required(CONF_ACTION_EVENT): event.event_schema(OlasActionEvent),
            **{
                cv.Required(key): button.button_schema(OlasActionButton)
                for key, _, _ in ACTIONS
            },
        }
    ),
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OlasGateway),
        cv.Required(CONF_RECEIVER_ID): cv.use_id(remote_base.RemoteReceiverBase),
        cv.Required(CONF_TRANSMITTER_ID): cv.use_id(remote_base.RemoteTransmitterBase),
        # May be empty: a new installation learns its remote before any fan is configured.
        cv.Required(CONF_DEVICES): cv.All(
            cv.ensure_list(DEVICE_SCHEMA), _unique_addresses
        ),
        cv.Optional(
            CONF_LEARN_START, default={CONF_NAME: "RF lernen (60 s)"}
        ): button.button_schema(OlasLearnButton),
        cv.Optional(
            CONF_LEARN_STOP, default={CONF_NAME: "RF Lernen stoppen"}
        ): button.button_schema(OlasLearnButton),
    }
).extend(cv.COMPONENT_SCHEMA)


async def _device_to_code(gateway, config):
    device = cg.new_Pvariable(
        config[CONF_ID], config[CONF_NAME], config[CONF_ADDRESS], config[CONF_CHECK_XOR]
    )
    cg.add(gateway.add_device(device))

    fan_var = await fan.new_fan(config[CONF_FAN], device)
    await cg.register_component(fan_var, config[CONF_FAN])
    cg.add(device.set_fan(fan_var))

    light_config = config[CONF_LIGHT]
    output = cg.new_Pvariable(light_config[CONF_OUTPUT_ID], device)
    await light.register_light(output, light_config)
    cg.add(device.set_light(await cg.get_variable(light_config[CONF_ID])))

    white = await select.new_select(
        config[CONF_WHITE_PRESET], device, options=WHITE_OPTIONS
    )
    cg.add(device.set_white_select(white))

    action_event = await event.new_event(
        config[CONF_ACTION_EVENT], event_types=EVENT_TYPES
    )
    cg.add(device.set_action_event(action_event))

    for key, _, constant in ACTIONS:
        await button.new_button(
            config[key], device, cg.RawExpression(f"::olas::{constant}")
        )


async def to_code(config):
    gateway = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(gateway, config)
    await remote_base.register_listener(gateway, config)
    transmitter = await cg.get_variable(config[CONF_TRANSMITTER_ID])
    cg.add(gateway.set_transmitter(transmitter))

    for device in config[CONF_DEVICES]:
        await _device_to_code(gateway, device)

    await button.new_button(config[CONF_LEARN_START], gateway, True)
    await button.new_button(config[CONF_LEARN_STOP], gateway, False)
