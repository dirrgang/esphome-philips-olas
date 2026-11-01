# ESPHome Philips Olas

Independent ESPHome gateway for Philips Olas ceiling fans, using an ESP32-S2
Mini and CC1101 at 434 MHz ASK/OOK. Not an official Philips/Signify product.
It integrates a six-speed Home Assistant fan, independent light ON/OFF,
three discrete white presets (**Warmweiß**, **Neutralweiß**, **Tageslichtweiß**),
original-remote reception and passive RF learning.

**Controls:** `Drehrichtung wechseln` toggles direction;
`Schlafmodus auslösen` requests sleep; three buttons request 1 h / 3 h / 6 h
timers. The native **Aktion** event entity reports `*_requested` (local
send request) and `*_received` (decoded remote command). Buttons have no
persistent active-mode/timer state. Sleep physically starts a stopped fan in
the tested setup; the HA ON indication is an estimate, not feedback.

**Known limits:** brightness and continuous Kelvin controls are not
implemented. Fan/light values represent commanded or overheard state, **not
physical telemetry**; reboot or lost RF can make them stale. The current
firmware controls **one RF ID per installed profile**. Device commands and
open questions belong to [protocol](docs/protocol.md), while HA state,
events and RF-proxy concurrency belong to
[architecture](docs/architecture.md). Dated hardware evidence and unresolved
work remain in [GitHub Issues](https://github.com/dirrgang/esphome-philips-olas/issues).

## Hardware

- ESP32-S2 Mini, using ESP-IDF.
- CC1101 433 MHz module with suitable antenna, powered at **3.3 V**.
- Wiring: [docs/hardware.md](docs/hardware.md).
- Tested ESPHome release: **2026.10.1**.

## Development

Use [mise](https://mise.jdx.dev/) to install the pinned toolchain:

```sh
mise install
mise run version
mise run test
```

The environment includes Python 3.13, uv 0.12.10 and ESPHome 2026.10.1.
Host tests need Bash/C++17 (Git Bash and pinned Zig on Windows).
[ESPHome firmware CI](.github/workflows/esphome.yml) checks a synthetic
configuration, rejects a broken include and compiles for ESP32-S2; it does
**not** flash hardware or prove RF behavior. See
[verification](docs/verification.md) for reproducible tests and evidence limits.

## Generic RF proxy diagnostic (experimental)

The separate receive-only CLI subscribes to the ESPHome native API without
changing firmware. Use verified private target parameters and an interactive
terminal:

```sh
mise run rf-diagnose -- <gateway-ip> --fan-id <id-hex> --check-xor <xor-hex>
```

Only after an authorized physical test, `--tx-light-on --post-rx` permits
a guarded LIGHT ON experiment requiring fresh remote LIGHT OFF and explicit
terminal `SEND`. Never transmit from CI or unattended agent runs.
See the [diagnostic procedure](docs/verification.md#generic-esphome-rf-proxy-diagnostic--23).

**Use one coordinated RF-proxy transmit owner.** Independent API clients
can displace each other's completion responses; a negative reply does not
prove no RF emission and must not be blindly retried. The proxy is not a
stock HA generic RF receiver. See
[transport limits](docs/architecture.md#shared-transmitter-limits-esphome-2026101).

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

Press **RF lernen (60 s)** in HA, then speed 1, speed 2 and OFF on the
intended original remote. Check `olas.learn` for one stable device ID and
check-XOR candidate across different commands and sequence values. Learning
is passive, ends automatically (or by **RF Lernen stoppen**) and does not
save a pairing.

Two independent physical units had different IDs/check constants, each
matching XOR of its three ID bytes. This is a useful derivation, **not**
a proven universal rule. Verify the candidate with multiple captures;
retain an explicit check-XOR override. See
[protocol/check derivation](docs/protocol.md#check-byte-and-per-device-xor-constant).
RF timing alone is not authentication.

Save the verified ID/check in the intended profile's **private**
`secrets.yaml`; enable `olas_control_enabled: "true"` and expose the fan
with `olas_fan_internal: "false"`. Validate, compile and install that
profile, then perform the
[physical smoke tests](docs/verification.md#physical-regression-matrix).
New profiles default to disabled control and hidden fan. A different device
requires its own capture and verification.

## Documentation

- [Protocol](docs/protocol.md): RF frame, opcode and physical-device knowledge.
- [Architecture](docs/architecture.md): components, HA state/events and shared
  CC1101/RF-proxy constraints.
- [Verification](docs/verification.md): reproducible tests and evidence rules.
- [Hardware](docs/hardware.md), [upstream attribution](docs/upstream.md) and
  [agent/contribution rules](AGENTS.md).
- [GitHub Issues](https://github.com/dirrgang/esphome-philips-olas/issues):
  remaining work and dated evidence.

## License and attribution

The repository uses the [MIT license](LICENSE). It builds on publicly
documented protocol research from
[kercle/philips-olas-remote](https://github.com/kercle/philips-olas-remote)
without depending on that firmware. See [upstream attribution and licensing](docs/upstream.md).
