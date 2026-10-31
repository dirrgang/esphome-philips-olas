# ESPHome Philips Olas

An independent ESPHome gateway for Philips Olas ceiling fans, using an
ESP32-S2 Mini and a CC1101 at 434 MHz ASK/OOK. It exposes a native six-speed
Home Assistant fan, independent on/off light and three discrete white presets,
receives original-remote commands and includes passive
RF discovery. It is not an official Philips/Signify integration.

Dimming is not implemented. Stateless buttons send direction toggle, sleep mode
and the 1 h / 3 h / 6 h timer commands through the same RF path as fan and light.
HA reflects commanded or overheard state, not independently confirmed telemetry.

**Drehrichtung wechseln** sends a toggle; it does not report forward/reverse.
**Schlafmodus auslösen** sends the captured sleep opcode without reporting an
active mode. The three **Timer** buttons report neither a running timer nor
remaining time. Sleep exit conditions and timer reset/replacement/cancellation
remain unknown. Direction toggle and original-remote synchronization are
user-confirmed on one test controller. Sleep/timer TX and physical effects still
need the [manual action tests](docs/verification.md#manual-stateless-action-tests).
Remote action commands synchronize the shared sequence and are logged, without
pressing buttons or changing estimated fan/light state. Sleep or an unobserved
timer expiry can therefore leave the fan estimate stale.

The light has no brightness or continuous Kelvin control. **Weißton** offers
Warmweiß, Neutralweiß and Tageslichtweiß; choosing one sends only that preset,
without an extra power command. Presets do not establish whether the light is on.
Remote power/preset commands update the corresponding entities without RF echo.
The preset starts unknown; the binary light starts OFF as an assumed UI default,
even if the physical lamp is on. Neither entity confirms state after RF loss,
missed commands or a restart. Startup/restoration sends no Olas RF. These new
entity paths require the [manual hardware checks](docs/verification.md).

Work items and execution evidence are maintained in
[GitHub Issues](https://github.com/dirrgang/esphome-philips-olas/issues) and PRs;
the [roadmap issue](https://github.com/dirrgang/esphome-philips-olas/issues/19)
links the related work. This repository documents behaviour and procedures.

## Hardware

- ESP32-S2 Mini, using ESP-IDF.
- CC1101 433 MHz module with suitable antenna, powered at **3.3 V**.
- Wiring: [docs/hardware.md](docs/hardware.md).
- Tested ESPHome release: **2026.10.1**.

## Development

Install [mise](https://mise.jdx.dev/), then run from the repository root:

```sh
mise install
mise run version
mise run test
```

Mise selects Python 3.13, uv 0.12.10 and an isolated ESPHome 2026.10.1 CLI.
Host tests require Bash and C++17. On Windows, install Git for Windows in its
standard location for Git Bash; mise supplies Zig 0.15.2. Unix uses GCC/Clang.
Transitive Python packages and firmware tools do not yet have a full lockfile.

## Generic RF proxy diagnostic (experimental)

The receive-only diagnostic uses ESPHome's encrypted native API and does not
modify firmware. It requires an interactive terminal and the intended device's
verified 24-bit RF ID and 8-bit check XOR. Use the IP from your private profile;
never commit private identifiers, keys, or raw RF captures:

```sh
mise run rf-diagnose -- <gateway-ip> --fan-id <id-hex> --check-xor <xor-hex>
```

To test a **single LIGHT ON transmit** on an already configured gateway,
append `--tx-light-on --post-rx`; a fresh original-remote LIGHT OFF and an
explicit interactive `SEND` confirmation are required. No CI or agent may
transmit. See [verification](docs/verification.md) and
[PoC #23](https://github.com/dirrgang/esphome-philips-olas/issues/23).
The API is experimental; these tests do not establish a stock HA receive feed
or mixed-client TX arbitration.

## Private hardware workspace

Create one private profile per installation from the generic template:

```sh
mise run init-local -- --profile site-a
mise run init-local -- --profile site-b
# Edit each profile's gateway.yaml, secrets.yaml and targets.toml.
mise run config-local -- --profile site-a
mise run compile-local -- --profile site-a
```

`init-local` preserves existing files. The entire **`esphome/local/`** directory
is gitignored: keep profiles, secrets, captures, logs, firmware and supplied
device exports there. Only generic templates belong in the public repository.
Each `esphome/local/<profile>/` contains its own:

- `gateway.yaml`: includes the shared gateway; supplies node/friendly/fan names,
  RF control settings and optional hardware/network/debug overrides.
- `secrets.yaml`: Wi-Fi, API/AP credentials and this target's fan ID/check.
- `targets.toml`: named upload/log destinations (IP/host or serial port).
- `.esphome/`: separate build/cache output for this profile.

All profiles include the same `esphome/olas-rf-gateway.yaml`; its protocol code
has no location-specific data or `!secret` lookups. Secrets are resolved explicitly
in the private profile and passed into that shared package. Do not create
`esphome/secrets.yaml`. Existing files there should be moved into the appropriate
private profile. New profiles disable control and hide the fan while leaving
learning available. The firmware currently controls **one fan ID per profile**.

Before updating an existing device, compare the profile with its actual live
configuration and preserve its Wi-Fi, API and encrypted OTA access. Compilation
does not install firmware. Configure destinations in that profile's `targets.toml`:

```toml
[targets.ota]
device = "192.0.2.10" # Actual IP/host, or "OTA" to resolve the profile's node name.

[targets.usb]
device = "COM7" # Or /dev/ttyACM0; use the currently enumerated serial port.
```

Build and install the selected profile over the chosen transport:

```sh
mise run install-local -- --profile site-a --target ota
mise run install-local -- --profile site-b --target usb
mise run logs-local -- --profile site-a --target ota
```

Network installation uses ESPHome OTA with that profile's encryption credentials;
logs use the encrypted native API. USB installation uses ESPHome's serial uploader;
some boards require entering the bootloader manually. `install-local` only uploads
after a successful build. `upload-local` is available for an already compiled
profile; `config-local` and `compile-local` never upload.

CLI selections override defaults in the ignored `mise.local.toml`:

```toml
[env]
OLAS_PROFILE = "site-a"
OLAS_TARGET = "ota"
```

Without a selection the default profile is `default`, target `ota`.
Machine-specific tool-cache overrides also belong in `mise.local.toml`.
`mise run config` and `mise run compile` validate/build `esphome/validation.yaml`,
a synthetic fixture using the same shared gateway. **Never install that fixture.**
`mise run test-profiles` verifies credential isolation and target selection without
hardware. The fallback AP has no web configuration interface.

## Discover a remote

In HA, press **RF lernen (60 s)**, then press speed 1, speed 2 and off on one
intended remote. Read the `olas.learn` logs. **RF Lernen stoppen** closes the
window early; it also closes automatically and starts inactive after reboot.
Learning never transmits or saves a pairing automatically.

Logs show a structural candidate ID, function, sequence, check and
`xor_candidate` (command XOR received check). Compare several commands and
sequences for a stable ID/check constant; timing recognition is not authentication.
Store the confirmed values only in that profile's private `secrets.yaml`, set
`olas_control_enabled: "true"` and `olas_fan_internal: "false"`, then validate,
compile, upload and perform the [hardware smoke test](docs/verification.md).

With control disabled, normal fan/light RX-state updates and Olas TX are gated,
and RF proxies are excluded. Light controls inherit `olas_fan_internal`; optional
`olas_light_internal` can hide them separately. With control enabled, fan, light
and RF proxies remain available alongside learning. Use one RF client at a time
until [#24](https://github.com/dirrgang/esphome-philips-olas/issues/24) stress and
failure tests are complete; the shared helper adds no custom queue. Other fans
need their own capture and compatibility test; no universal check constant is assumed.

## Documentation

- [GitHub Issues](https://github.com/dirrgang/esphome-philips-olas/issues): work and evidence.
- [Protocol](docs/protocol.md): frame format, commands and open hypotheses.
- [Verification](docs/verification.md): development and hardware test procedure.
- [Architecture](docs/architecture.md): codec, ESPHome glue and boundaries.
- [Upstream attribution](docs/upstream.md) and [agent instructions](AGENTS.md).

## License and attribution

Original code is [MIT licensed](LICENSE). The independent
[kercle/philips-olas-remote](https://github.com/kercle/philips-olas-remote)
project provides MIT-licensed standalone ESP8266 firmware and protocol research.
This project uses ESPHome's own CC1101, RX/TX and native entity components;
it does not depend on that firmware or copy its source files.
