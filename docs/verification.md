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
Discovery tests also cover unknown IDs/non-default checks, multiple devices in
one batch, malformed pulses, invalid tail bits and normal RX filtering.

## Integrated passive discovery

Learning is part of `esphome/olas-rf-gateway.yaml`; use the usual `config` and
`compile` tasks. An ignored `mise.local.toml` can set `OLAS_LOCAL_CONFIG` to
a private overlay under `esphome/local/`, with its own network/API secrets,
for `config-local` and `compile-local`. Keep locations and captures out of Git.

Create the workspace with `mise run init-local`. It copies the generic profile
and secrets examples to `esphome/local/` without overwriting existing files.
Fill the private secrets, then run `mise run config-local` and
`mise run compile-local`. Upload is a separate explicit `mise run upload-local`;
`mise run logs-local` reads device logs. `OLAS_DEVICE` selects a USB port or
address; the default is OTA. Store original exports, diagnostics and results
inside this ignored directory, not beside the public examples.

For an unknown target, use `olas_control_enabled: "false"` and
`olas_fan_internal: "true"`. Normal RX-state updates and fan TX are then
disabled and the RF proxy package is excluded. The internal fan cannot be
controlled from HA. No placeholder-derived RF command should be transmitted.
The learning feature itself is passive, including when normal control is on.
After comparing against the actually flashed config and preserving access,
upload the regular gateway with the selected profile, connect HA and open logs.
Press **RF lernen (60 s)**, then deliberately press speed 1, speed 2 and off
on exactly one remote. Stop early with **RF Lernen stoppen** if needed.

Expected: stable ID, matching function codes, changing 2-bit sequence and a
consistent `xor_candidate` across several presses. The log labels all frames
as candidates because their check rule is unknown. Noise and unrelated devices
can still match the pulse structure; timing recognition is not authentication.
Multiple different candidates in one batch are examined. Consecutive copies
of the same raw frame within 400 ms are suppressed only in diagnostic logging.

Confirm that no candidate logs appear outside the window, the window ends
after 60 seconds and reboot leaves it inactive. Record sanitized observations
in the relevant GitHub issue/PR and keep raw captures private. Only then fill
the intended device's RF settings, enable RF control
and expose the fan again. No persistent pairing or TX validation is claimed.

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

## Physical regression matrix

This is a reusable test procedure, not an execution tracker. Record firmware
revision, sanitized observations and acceptance progress in the relevant issue
or PR. [Issue #3](https://github.com/dirrgang/esphome-philips-olas/issues/3)
contains baseline regression evidence; a smoke test does not establish every
edge case. Keep actual identifiers and captures in the ignored workspace.

| Action | Expected |
|---|---|
| HA: off → speed 1 | Fan starts, no errors |
| HA: speed 1 → 3 → 6 | Correct fan speed |
| HA: speed 6 → off | Stops, last speed retained |
| HA: off → speed 4 | Starts at 4 |
| Remote: speed 1 → 6 | HA sync without RF echo |
| Remote: off | HA updates OFF, no TX |
| Remotes and HA alternating | Seq remains operable |
| Two quick presses of same function | Do not lose genuine press |
| Restart ESP32 with fan running | Do not claim confirmed state |

## Targeted dimming experiment

Capture timestamp and raw event count for (1) short `brightness+`, (2) hold
`brightness+` ~3 seconds, (3) hold `brightness−` ~3 seconds. Capture within a
limited controlled window to avoid collecting unrelated RF identifiers.

## Handling a validation failure

1. Make **one change at a time**, compare against last known-good flashed YAML.
2. Recheck actual RX/TX pins, SPI, CC1101 Chip ID, and mode transitions.
3. Collect only the minimal relevant logs and observed physical behaviour.
4. Revert instead of stacking unverified changes.
