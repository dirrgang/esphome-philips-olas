# Validation procedure

## Host tests

```bash
./scripts/test.sh
```

Tests include: bit packing, ID/check validation, seq wrap, six speeds and off,
RAW decode with unrelated leading pulses, repeated frames and truncated input.

## ESPHome compile (not yet run for the refactored repository)

```bash
cp esphome/secrets.yaml.example esphome/secrets.yaml
# EDIT THE FILE: replace Wi-Fi, HA API and OTA credentials.
esphome config esphome/olas-rf-gateway.yaml
esphome compile esphome/olas-rf-gateway.yaml
```

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
