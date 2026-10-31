# Architecture

## Present architecture

```
Original Olas remote  ──433 MHz──▶ CC1101 ──RMT/SPI──▶ ESP32-S2 ──API──▶ Home Assistant
Home Assistant         ──API──────▶ ESP32-S2 ──RMT/SPI──▶ CC1101 ──433 MHz──▶ Fan
```

- The receiver scans raw marks/spaces for a valid frame; the pure helper
  `esphome/include/olas_protocol.h` checks ID, checksum and framing.
- Fan entity `on_state` encodes 41 bits, sends four identical copies, and
  decrements the local 2-bit sequence counter.
- RX `publish_state()` can trigger `on_state`; `olas_rf_update` prevents echo.
- Physical remote presses update the *commanded/observed* HA state.
- Passive learning uses the same RX callback with an opt-in 60-second window;
  unknown frames remain candidates, never automatic pairings or TX triggers.
- Per-installation overlays and artifacts live under ignored `esphome/local/`;
  public code and synthetic host fixtures are independent of those files.
- Each profile resolves its own secrets and passes substitutions to the same
  shared gateway package. That package never performs a `!secret` lookup.
  Profile-local target files select IP/OTA or serial deployment independently
  of the firmware configuration. Builds are isolated inside each profile.

## Why code is separated this way

- The C++ header is a **pure transport-independent protocol codec**: host
  unit-testable and shareable with an eventual external component.
- The ESPHome YAML remains thin glue to existing CC1101 and `fan` components.
- We deliberately avoid copying upstream's ESP8266 driver or web UI because
  ESPHome already manages Wi-Fi, OTA, API, CC1101 and GPIOs.

Architecture proposals and implementation sequencing are tracked in
[GitHub Issues](https://github.com/dirrgang/esphome-philips-olas/issues).
This document describes the implementation in this checkout.

## Known modelling traps

- **No ACK:** publishing a command is not proof of actuation.
- **Light brightness:** repeated up/down is relative, not automatically a
  truthful 0–100% brightness sensor.
- **Direction toggle:** physical direction may be unknown after reboot or
  missed RF; avoid advertising an absolute state without reconciliation.
- **Duplicate RF bursts:** suppress repeated frames but don't suppress a
  genuinely new press of the same button.
- **Simultaneous remotes and ESP32:** each received matching frame advances
  the local next-sequence value; sequence wrap is not authentication.

## Experimental receive-only multi-device core (PR #21)

An optional external ESPHome component lives in `components/olas/`; it is
not part of the currently deployed shared YAML/package. It uses a byte-identical copy of the single-device gateway's pure 41-bit
codec, checked by `scripts/test-core.sh`. ESPHome copies `esphome.includes`
headers into generated build directories, so a relative forwarding include to
the optional component fails compilation; both standalone headers deliberately
remain self-contained until architecture ownership is decided. The lightweight `GatewayCore` scans one
receiver batch and dispatches structurally valid candidate frames to configured
24-bit addresses. Each logical device owns its check XOR, independently
estimated fan state, descending 2-bit sequence and bounded dedup history.
It does **not** perform RF TX, retune CC1101, or register native HA entities.

The synthetic fixture `tests/fixtures/olas-core.yaml` validates the schema,
typed receiver/transmitter references and two distinct logical addresses;
it must not be installed. The choice between keeping this component and
moving protocol/entity ownership to Home Assistant is explicitly deferred
to the [architecture ADR #27](https://github.com/dirrgang/esphome-philips-olas/issues/27).
The current private-profile gateway and its learning flow remain authoritative
until an explicitly approved and physically verified migration.
