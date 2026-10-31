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

Learning is part of the shared `esphome/olas-rf-gateway.yaml` package. Create
a private profile with `mise run init-local -- --profile site-a`. It creates
`gateway.yaml`, `secrets.yaml` and `targets.toml` under ignored
`esphome/local/site-a/`, preserving existing files. The profile resolves all
secrets itself, then passes values to the shared package; a root secrets file
must never select another installation's credentials.

Fill the profile's names, RF parameters, credentials and targets, then run
`mise run config-local -- --profile site-a` and `compile-local` with the same
selection. `install-local -- --profile site-a --target ota` builds and uploads
over the configured IP/host; `--target usb` selects that profile's serial port.
Use `logs-local` with the same profile/target for diagnostics. Defaults may be
set through `OLAS_PROFILE` and `OLAS_TARGET` in ignored `mise.local.toml`.
Store private exports and raw captures inside the selected profile.

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

## Generic ESPHome RF-proxy diagnostic — #23

Independent of the existing ESPHome Olas fan entity, the optional receive-only
script subscribes to native-API raw RF events and decodes the known 41-bit Olas
protocol. It uses an isolated pinned `aioesphomeapi` Python script dependency,
no OTA, no network access during host-only unit tests, and requires an interactive
terminal to read the encryption key without echoing it.

```bash
mise run rf-diagnose -- <gateway-ip> --fan-id <id-hex> --check-xor <xor-hex>
# Deliberate physical experiment only, with suitable antenna and CC1101 at 3.3V:
mise run rf-diagnose -- <gateway-ip> --fan-id <id-hex> --check-xor <xor-hex> --tx-light-on --post-rx
```

Keep actual ID, XOR and API key in the ignored profile or private working notes,
not in committed examples. The script requires remote LIGHT OFF as a fresh
sequence baseline, an explicit terminal `SEND`, then asks for physical
verification and optional independent post-TX remote LIGHT OFF RX.
The API request itself does not acknowledge over-air delivery.

**Observed on deployed ESPHome 2026.10.1 firmware, 2026-10-31 (#23):**
an independent API client decoded four valid Olas frames per physical remote
press from 385-timing batches, sent LIGHT ON via the proxy and the user
**physically confirmed the light switched on**. The original remote subsequently
switched it off and a post-TX raw RX event decoded correctly. This establishes
basic proxy RX/TX feasibility, **not** reliable concurrent client arbitration,
native HA receiver support, multiple real device addresses, or physical testing
of the repository's refactored YAML. See #24 and #27.

### Native Windows console compatibility

For the mandatory interactive `SEND` prompt, native Windows uses an
asynchronous `msvcrt` key poller instead of `asyncio.add_reader()` (unsupported
by Windows ProactorEventLoop, and the Windows selector loop cannot watch console
handles). The prompt remains bounded and rejects a noninteractive stdin;
there is no background input thread left running after timeout. The same
script uses `add_reader()` in POSIX terminals. Host tests run on Linux and
native Windows; physical RF TX was verified by the user on Linux only.

## ESPHome validation/compile

Run with the ESPHome 2026.10.1 CLI installed by mise; these commands do not
flash the device and do not use real credentials. They use the synthetic
`esphome/validation.yaml` fixture, which includes the regular shared package.

```bash
mise run test-profiles
mise run config
mise run compile
```

The profile tests deliberately place conflicting secrets beside the shared
package, then validate two isolated profiles with different synthetic data.
They also verify target selection and that a failed build prevents installation.
They never contact hardware. Never flash the synthetic build fixture.

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
