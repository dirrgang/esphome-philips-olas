# Validation procedure

## Host tests

```bash
mise install
mise run test
```

This task invokes `bash scripts/test.sh`. Bash and a C++17 compiler are
required. On native Windows, use Git Bash from Git for Windows; `mise install`
provides Zig 0.15.2, whose C++ frontend runs the same test suite. On Linux/macOS,
install GCC/Clang. CI continues to run the same test script directly.

Tests include: bit packing, ID/check validation, seq wrap, six speeds and off,
RAW decode with unrelated leading pulses, repeated frames and truncated input.

## ESPHome validation/compile

Run with the ESPHome 2026.10.1 CLI installed by mise; these commands do not
flash the device.

```bash
cp esphome/secrets.yaml.example esphome/secrets.yaml
# EDIT THE FILE: use the existing Wi-Fi, API and fallback AP secrets.
# Keep the productive OTA settings; this reference uses encrypted OTA.
mise run config
mise run compile
```

On 2026-10-31, native Windows `mise run test`, ESPHome 2026.10.1
`mise run config` and `mise run compile` passed using the supplied local
production secrets. The productive YAML export was compared with the
reference. A local `mise.local.toml` pins the physical ESP-IDF cache path to
avoid a Windows desktop-app path mismatch; see `docs/STATUS.md` for details.
Both `firmware.ota.bin` and `firmware.factory.bin` were generated in
`esphome/.esphome/build/olas-rf-gateway/build/`. No upload was performed;
the physical matrix below remains pending.

## Physical test matrix (must be performed after merging the refactor)

| Action | Expected | Actual |
|---|---|---|
| HA: off → speed 1 | Fan starts, no errors | pending |
| HA: speed 1 → 3 → 6 | Correct fan speed | pending |
| HA: speed 6 → off | Stops, last speed retained | pending |
| HA: off → speed 4 | Starts at 4 | pending |
| Remote: speed 1 → 6 | HA sync without RF echo | pending |
| Remote: off | HA updates OFF, no TX | pending |
| Remotes and HA alternating | Seq remains operable | pending |
| Two quick presses of same function | Do not lose genuine press | pending |
| Restart ESP32 with fan running | Do not claim confirmed state | pending |

Mark these as tested only after on-device verification; the equivalent
pre-refactor features worked in the prior prototype.

## Targeted dimming experiment

Capture timestamp and raw event count for (1) short `brightness+`, (2) hold
`brightness+` ~3 seconds, (3) hold `brightness−` ~3 seconds. Capture within a
limited controlled window to avoid collecting unrelated RF identifiers.

## Handling a validation failure

1. Make **one change at a time**, compare against last known-good flashed YAML.
2. Recheck actual RX/TX pins, SPI, CC1101 Chip ID, and mode transitions.
3. Collect only the minimal relevant logs and observed physical behaviour.
4. Revert instead of stacking unverified changes.
