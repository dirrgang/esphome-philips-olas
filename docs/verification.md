# Validation procedure

## Host tests

```bash
mise install
mise run test
```

This task invokes `bash scripts/test.sh`. Bash and a C++17 compiler are
required (use WSL on Windows). CI continues to run the same test script directly.

Tests include: bit packing, ID/check validation, seq wrap, six speeds and off,
RAW decode with unrelated leading pulses, repeated frames and truncated input.
The multi-device core suite adds two synthetic address/check pairs, mixed-address
batches, independent counters/filter/state, malformed and unknown frames, retained
speed while off, unmapped functions and millisecond-counter wrap.

## ESPHome validation/compile

Run with the ESPHome 2026.10.1 CLI installed by mise; these commands do not
flash the device.

```bash
cp esphome/secrets.yaml.example esphome/secrets.yaml
# EDIT THE FILE: replace Wi-Fi, HA API and OTA credentials.
mise run config
mise run compile
```

If `esphome/secrets.yaml` already exists, keep it; do not overwrite live
credentials. Config output and generated sources can contain secrets, so retain
them locally and do not attach full configuration/build artifacts to issues.

For the receive-only external-component fixture (no secrets required):

```bash
mise run test-schema
mise run config-core
mise run compile-core
```

`test-schema` invokes the pinned CLI on positive and negative configurations,
including duplicate addresses/IDs, empty/missing devices, invalid address/check
values (including booleans) and missing/wrong-type RX/TX references. CI runs host
and schema suites; firmware builds are local checks in this PR. The fixture uses
synthetic pairs and has no entities or transmit actions. **Never flash it.**

## Recorded local evidence — 2026-10-31

Environment: mise-managed Python 3.13.18, uv 0.12.10, ESPHome **2026.10.1**,
ESP-IDF **5.5.6**, target **ESP32-S2**. All commands below were actually run.

| Tier | Command | Observed result |
|---|---|---|
| Toolchain | `mise install` | All three pinned tools already installed; succeeded |
| Host C++17 | `mise run test` | `Olas protocol tests passed`; `Olas multi-device core tests passed` |
| ESPHome schema | `mise run test-schema` | 8 unittest methods and their invalid-input subcases passed |
| Reference config | `mise run config` | `Configuration is valid!` (local secrets withheld) |
| Core fixture config | `mise run config-core` | `Configuration is valid!` |
| Reference firmware | `mise run compile` | `Successfully compiled program.` |
| Core fixture firmware | `mise run compile-core` | `Successfully compiled program.` |
| Physical hardware | No flash or smoke test | **Unverified** (#3 / #20) |

Static build reports: reference RAM 120,956 / 172,032 bytes (70.3%), flash
763,478 / 1,835,008 bytes (41.6%); core fixture RAM 62,540 / 172,032 bytes
(36.4%), flash 245,462 / 1,835,008 bytes (13.4%). These are build metrics, not
runtime free-heap measurements. The minimal fixture omits Wi-Fi/API/entities and
cannot predict resource usage of the future two-fan integration.

Non-fatal notices: the existing reference warns about OTA password overhead and
implicit `non_blocking: true`; the fixture sets nonblocking explicitly. The
toolchain reported a missing `esp_blockdev` CMakeLists and a read-only component
list cache on its first fixture build; compilation still succeeded. No baseline
RF settings or credential files were changed to silence these notices.

The user-supplied 2026-10-30 build is also recorded in
[#2](https://github.com/dirrgang/esphome-philips-olas/issues/2#issuecomment-6533201667).
Compilation does not establish physical actuation, correct RMT capture under
load, the second remote's address/check, or independent control of both fans.

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
