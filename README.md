# ESPHome Philips Olas

An independent ESPHome gateway for Philips Olas ceiling fans, using an
ESP32-S2 Mini and a CC1101 at 434 MHz ASK/OOK. It exposes a native six-speed
Home Assistant fan, receives original-remote commands and includes passive
RF discovery. It is not an official Philips/Signify integration.

Light, dimming, direction, sleep and timers are not implemented as entities.
HA reflects commanded or overheard state, not independently confirmed telemetry.

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

## Private hardware workspace

Create a private profile from the supplied templates:

```sh
mise run init-local
# Edit esphome/local/secrets.yaml with your own Wi-Fi/API/AP credentials.
mise run config-local
mise run compile-local
```

`init-local` preserves existing files. The entire **`esphome/local/`** directory
is gitignored: keep profiles, secrets, captures, logs, firmware and supplied
device exports there. Only generic templates belong in the public repository.
The local profile includes the regular gateway and initially disables control
and hides the fan while leaving the learning buttons available.

Before updating an existing device, compare the profile with its actual live
configuration and preserve its Wi-Fi, API and encrypted OTA access. Compilation
does not install firmware. After that comparison, upload explicitly:

```sh
mise run upload-local
mise run logs-local
```

These tasks default to OTA. Set `OLAS_DEVICE` to a USB port when needed, or
invoke ESPHome directly for bootloader/reset-specific handling. Select another
private profile through `OLAS_LOCAL_CONFIG`. Machine-specific environment
settings and tool-cache overrides belong in the gitignored `mise.local.toml`:

```toml
[env]
OLAS_LOCAL_CONFIG = "esphome/local/gateway.yaml"
# OLAS_DEVICE = "COM7"
```

The reference can also be validated directly: copy `esphome/secrets.yaml.example`
to `esphome/secrets.yaml`, fill all actual credentials **and RF parameters**,
then run `mise run config` and `mise run compile`. The example RF values are
disabled sentinels, not a working pairing. Use the private discovery profile
when the target is unknown. The fallback AP has no web configuration interface.

## Discover a remote

In HA, press **RF lernen (60 s)**, then press speed 1, speed 2 and off on one
intended remote. Read the `olas.learn` logs. **RF Lernen stoppen** closes the
window early; it also closes automatically and starts inactive after reboot.
Learning never transmits or saves a pairing automatically.

Logs show a structural candidate ID, function, sequence, check and
`xor_candidate` (command XOR received check). Compare several commands and
sequences for a stable ID/check constant; timing recognition is not authentication.
Store the confirmed values only in the private `secrets.yaml`. In the profile,
replace the disabled RF sentinels with their `!secret` references, set
`olas_control_enabled: "true"` and `olas_fan_internal: "false"`, then validate,
compile, upload and perform the [hardware smoke test](docs/verification.md).

With control disabled, normal fan RX-state updates/TX are gated, the fan is
hidden and RF proxies are excluded. With control enabled, the regular fan and
RF proxies remain available alongside learning. Other fans need their own
capture and compatibility test; no universal check constant is assumed.

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
