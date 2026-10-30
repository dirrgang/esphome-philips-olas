# ESPHome Philips Olas (work in progress)

An ESP32-S2 + CC1101 gateway that sends and receives Philips Olas ceiling-fan
433/434 MHz RF commands, with a native Home Assistant `fan` entity.

**Status (2026-10-30):** Fan power and six speeds were tested successfully on
real hardware, from both Home Assistant (TX) and the original remote (RX). The
pure C++ protocol module and the project-formatted YAML configuration in this
repository were **reconstructed/refactored** from the working experiment;
they pass host tests but have **not yet been recompiled/flashed together**.
Light, temperature, dimming, reversal, sleep and timers are documented or
captured but **not implemented as entities**.

The project is **independent**, not an official Philips/Signify integration,
and is not a fork of the original standalone firmware.

## Why a separate repository?

The upstream [kercle/philips-olas-remote](https://github.com/kercle/philips-olas-remote)
provides an ESP8266/PlatformIO standalone controller and extensive protocol
research under the MIT license. This repository instead targets the built-in
ESPHome CC1101 driver, ESPHome native API and Home Assistant entities. We credit
upstream and plan small, focused upstream contributions for genuinely reusable
protocol discoveries. We do not copy upstream source files.

## Hardware

- LOLIN-style **ESP32-S2 Mini** (USB, Wi-Fi)
- **CC1101 433 MHz ASK/OOK** module with 433 MHz antenna (3.3 V)
- Eight wires; see [docs/hardware.md](docs/hardware.md)
- ESPHome **2026.10.1** was the tested firmware version

## Run the reference configuration

1. Check the eight connections in [hardware.md](docs/hardware.md).
2. Copy `esphome/secrets.yaml.example` to `esphome/secrets.yaml` and fill in
   your **existing** Wi-Fi, API encryption and OTA credentials before flashing.
3. If changing an existing ESPHome node, **merge changes into your existing
   YAML first**, rather than overwriting working credentials or OTA settings.
4. From the repo root, run `esphome config esphome/olas-rf-gateway.yaml`.
5. Inspect the configuration and flash only after validation; test a single
   speed change, then six speeds/off, then original remote reception.
6. Run `./scripts/test.sh` for the standalone protocol unit tests.

The example contains an observed **device-specific** 24-bit fan ID and check
XOR constant in `secrets.yaml.example`. Other Olas remotes require individual
capture and may use a different check value. No generic pairing is implemented.

## Documentation

- [docs/STATUS.md](docs/STATUS.md): authoritative project state and acceptance criteria
- [docs/protocol.md](docs/protocol.md): frame, commands, observations and open hypotheses
- [docs/hardware.md](docs/hardware.md): verified GPIO wiring
- [docs/architecture.md](docs/architecture.md): code organization and migration path
- [docs/upstream.md](docs/upstream.md): contribution proposal for upstream
- [docs/verification.md](docs/verification.md): reproducible hardware test matrix
- [AGENTS.md](AGENTS.md): instructions for coding agents / Codex

## Licensing

Our original code is provided under [MIT](LICENSE). The upstream project is also
MIT licensed, but remains a separate project; see attribution above and
[docs/upstream.md](docs/upstream.md).
