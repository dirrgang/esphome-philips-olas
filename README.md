# ESPHome Philips Olas (work in progress)

An ESP32-S2 + CC1101 gateway that sends and receives Philips Olas ceiling-fan
433/434 MHz RF commands, with a native Home Assistant `fan` entity.

**Status (2026-10-31):** Fan power and six speeds were tested successfully on
real hardware, from both Home Assistant (TX) and the original remote (RX). The
pure C++ protocol module and the project-formatted YAML configuration in this
repository were **reconstructed/refactored** from the working experiment;
they pass host tests and ESPHome 2026.10.1 config/compilation, but their
**physical regression remains unverified**. A receive-only multi-device external
component core is available for development; native entity migration is pending.
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

## Development setup with mise

Install [mise](https://mise.jdx.dev/) once, then from the repository root:

```sh
mise install
mise run version
mise run test
```

`mise.toml` selects Python 3.13, uv 0.12.10 and **ESPHome 2026.10.1**
installed as an isolated PyPI CLI via mise's `pypi:` backend. No global
`pip install` and no project Python virtual environment are needed.

To check or compile the firmware, first create your **local untracked**
`esphome/secrets.yaml` from `esphome/secrets.yaml.example`, using the
credentials of your *existing* ESPHome device:

```sh
mise run config
mise run compile
```

The `config` and `compile` tasks do **not** flash hardware. The `test`
task runs `scripts/test.sh`, so it also requires Bash and a C++17 compiler
(e.g. GCC/Clang). On Windows, use WSL for this workflow; mise itself supports
Windows, but the existing shell-based host test requires a Unix-like toolchain.

Mise pins the ESPHome release and uv version; transitive Python packages and
the PlatformIO toolchain are not yet represented by a full dependency lock.
The repository's refactored YAML compiles; it has not been regression-tested on
the physical device. See [verification](docs/verification.md) for evidence tiers.

For the experimental external-component core, use the synthetic fixture:

```sh
mise run test-schema
mise run config-core
mise run compile-core
```

This fixture has two synthetic address/check pairs and no secrets. **Never flash
it**: it exposes no entities and has no RF send path. The existing single-device
reference remains the migration baseline. See [architecture](docs/architecture.md).

## Run the reference configuration

1. Check the eight connections in [hardware.md](docs/hardware.md).
2. Copy `esphome/secrets.yaml.example` to `esphome/secrets.yaml` and fill in
   your **existing** Wi-Fi, API encryption and OTA credentials before flashing.
3. If changing an existing ESPHome node, **merge changes into your existing
   YAML first**, rather than overwriting working credentials or OTA settings.
4. From the repo root, run `mise run config` (or the equivalent `esphome config` command).
5. Inspect the configuration and flash only after validation; test a single
   speed change, then six speeds/off, then original remote reception.
6. Run `mise run test` for the standalone protocol unit tests.

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
