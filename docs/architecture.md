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
