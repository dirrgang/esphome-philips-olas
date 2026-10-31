# Architecture

## Present architecture

```
Original Olas remote  ──433 MHz──▶ CC1101 ──RMT/SPI──▶ ESP32-S2 ──API──▶ Home Assistant
Home Assistant         ──API──────▶ ESP32-S2 ──RMT/SPI──▶ CC1101 ──433 MHz──▶ Fan
```

- The receiver scans raw marks/spaces for a valid frame; the pure helper
  `esphome/include/olas_protocol.h` checks ID, checksum and framing.
- Fan and binary-light `on_state`, the white-preset select's `set_action`,
  and direction/sleep/timer buttons' `on_press`
  invoke the same synchronous `olas_send` script. The portable `send_command`
  template uses the existing transmitter's TransmitCall API, encodes 41 bits,
  submits four identical copies, and decrements the shared 2-bit sequence once.
- RX `publish_state()` can trigger `on_state`; a scoped `olas_rf_update` guard
  prevents echo. Light RX publishes remote/current values directly. Its template
  binary output is inert because output writes occur later in the light loop,
  beyond the synchronous guard. Only synchronous publication requests RF.
- `olas_ready` enables Olas sends after component setup (`on_boot: -100`). Both
  fan restore publication and light startup publication are suppressed for RF.
  Fan speed/off mapping and last nonzero speed retention remain unchanged.
- Physical remote presses update the *commanded/observed* HA state.
- Direction/sleep/timer RX updates only the same next-sequence and command log.
  It never presses a button, retransmits or invents an absolute mode/timer state.
  These five template buttons inherit `olas_fan_internal` and the shared sender's
  control/startup/RX guards. No new counter, protocol or transport is introduced.
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
- The ESPHome YAML remains thin glue to existing CC1101 and native components.
- We deliberately avoid copying upstream's ESP8266 driver or web UI because
  ESPHome already manages Wi-Fi, OTA, API, CC1101 and GPIOs.

Architecture proposals and implementation sequencing are tracked in
[GitHub Issues](https://github.com/dirrgang/esphome-philips-olas/issues).
This document describes the implementation in this checkout.

## Light representation and transport limits

ESPHome **2026.10.1** provides `binary` light with `ON_OFF` traits and a
`template` binary output; it does not need a custom light platform. Continuous
color-temperature/brightness traits would misrepresent this device, so a native
template select supplies exactly three white presets instead. Its lambda has no
initial or restored option and publishes only commanded/overheard presets.
The light uses `ALWAYS_OFF`: OFF after boot is an assumption, not a measurement.
Preset commands do not change the estimated power state. No ACK is available.

Inspected installed pinned sources (with tagged upstream references):
[TransmitCall/send_](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/remote_base/remote_base.cpp),
[ESP32 RMT](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/remote_transmitter/remote_transmitter_rmt.cpp),
[binary light](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/binary/light/binary_light_output.h),
[LightState](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/light/light_state.cpp),
and [template fan](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/template/fan/template_fan.cpp).
The ESP32/IDF >=5.5.1 nonblocking sender flushes pending completion before
encoding the next RMT buffer. It waits for the previous transmission and fires
completion before the next TX hook. This supports consecutive calls on the same
transmitter from the main loop; it is not a general thread-safe queue. Keep hooks
limited to CC1101 TX/RX mode changes; do not start reentrant sends from them.

The proxy continues to use that same transmitter. No second sender, frequency
retuning or custom queue is introduced. Completion is not a device ACK. Mixed
proxy/Olas short-gap, failure recovery and frequency handling remain hardware
gates in [#24](https://github.com/dirrgang/esphome-philips-olas/issues/24): use one
client at a time and wait for completion/RX recovery before switching clients.
Generic proxy commands also bypass the Olas sequence/state helper; observe a
fresh original-remote command before resuming native control. This is an
incremental extension of the working gateway, not a decision on
[#27](https://github.com/dirrgang/esphome-philips-olas/issues/27) or PR #21.

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
