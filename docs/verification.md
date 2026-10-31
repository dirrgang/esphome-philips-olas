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
The shared sender is exercised through a fake TransmitCall: all five light
commands, direction/sleep/three timer actions and six speeds/off,
independent MSB-first pulse/check expectations,
four identical copies, one shared descending sequence including wraparound,
invalid IDs/checks, startup/control-disabled gates and guarded RX callbacks
that cannot submit TX. This does not execute ESPHome on hardware.
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
or PR. Use neutral device labels in public reports; omit installation locations,
private profile names, personal identifiers and private network addresses.
[Issue #3](https://github.com/dirrgang/esphome-philips-olas/issues/3)
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

## Manual light and shared-TX regression

Use a reviewed private profile, preserve the working firmware/configuration for
rollback, and compare with the installed configuration before an authorized
flash. Use 3.3 V and a suitable 433 MHz antenna. These are manual procedures;
CI/Codex must not flash or send RF. Record **RX logs**, **TX request/completion
logs**, and **independent physical response** separately in the PR / issues
[#5](https://github.com/dirrgang/esphome-philips-olas/issues/5),
[#8](https://github.com/dirrgang/esphome-philips-olas/issues/8),
[#14](https://github.com/dirrgang/esphome-philips-olas/issues/14) and
[#24](https://github.com/dirrgang/esphome-philips-olas/issues/24).

| Manual action | RX / HA check | TX check | Physical check |
|---|---|---|---|
| HA light ON `0x7C`, then OFF `0xBC`, fan stopped and running | HA power estimate independent of fan | One logical request each, four identical copies; next sequence once | Lamp follows; fan unaffected |
| HA Warmweiß `0x6C`, Neutralweiß `0x64`, Tageslichtweiß `0x84` | Exactly three select choices; no brightness/Kelvin | Each code individually, no extra power command | Observe each white tone, power side effects and fan behaviour separately |
| Remote each of the five light commands | Matching HA power or preset; other estimate retained | No Olas TX request from RX publication | Lamp responds to remote; no second action/echo |
| Alternate HA fan/light and remote fan/light through sequence wrap | Shared next-sequence follows accepted remote commands | Correct code/sequence, unchanged timings/copies | All six fan speeds/off and light still work |
| Repeat identical button twice, then rapid distinct commands | New sequence accepted; existing 400 ms exact-frame dedup retained | Both deliberate HA requests submitted; copies do not consume sequences | Legitimate presses retained; record losses rather than assume delivery |
| Restart ESP with lamp/fan running; remote use while gateway offline | Light OFF is a UI assumption, preset unknown; stale fan state documented | No Olas startup/restore TX | No physical change caused by restart; deliberate next command recovers estimate |
| Native fan → proxy LIGHT ON → native light OFF, with completion/RX recovery between clients | Receive a fresh remote press before returning to native sequence tracking | Same transmitter, TX/RX hooks recover; no concurrent clients | Record every transition independently; stop on RX loss |

Generic proxy LIGHT ON and subsequent original-remote LIGHT OFF have physical
evidence in #8/#24. Native light ON/OFF, all three white presets and remote/HA
synchronization were subsequently confirmed by the user for PR #31 on one
controller; see #8/PR #31 for execution evidence. This does not establish proxy
LIGHT OFF TX or completion of the remaining regression rows. Overlapping-client,
failure and retuning stress tests remain #24 work.

## Manual stateless action tests

Use the reviewed private profile and preserve installed access/rollback as above.
These tests require deliberate user operation; builds and CI never send RF.
Record firmware revision, RX and TX-request/completion logs, and observed physical
effects separately in #11/#12/#13 and the PR, using neutral device labels.

| Action | RX / HA expectation | TX expectation | Physical observation to record |
|---|---|---|---|
| Direction toggle `0x50`, under manufacturer-permitted operating conditions | Remote press logged, sequence synchronized; no absolute direction | One shared-path request per deliberate HA press; no request from RX | Actual reversal and any stop/start behaviour; repeat only deliberately |
| Sleep `0x30` at several starting speeds | No assumed active mode or speed update | Same shared path and four copies | Observable pattern/side effects; repeat semantics; whether speed or OFF exits sleep |
| Each timer `0x1C` / `0x18` / `0x14` | No active timer, remaining time or automatic fan-OFF estimate | Correct requested code and shared sequence | Initial response separately from a fully observed 1 h / 3 h / 6 h expiry |
| Successive timer choices, repeated same timer, then speed/OFF | No inferred cancellation or replacement | Each deliberate press is one action | Reset, replacement, toggle/cancel behaviour; mark unknown if not observed |
| Remote action → HA fan/light → remote action through wraparound | Sequence stays shared; no button replay or RX echo | Unchanged fan/light waveforms; no second sender | Device accepts alternating controls; all six speeds/off and light regressions |
| Restart and control-disabled profile | No restored action or invented mode state; buttons hidden with fan | No startup TX; disabled control rejects actions | No unintended action; retain original-remote operation |

The five new TX paths and their physical effects remain unverified until these
tests are performed. Do not infer a full timer timeout test from an immediate
response. Sleep and timer expiry may leave HA's previous fan estimate stale.
Mixed-client arbitration remains separate #24 work.

## Targeted dimming experiment

Capture timestamp and raw event count for (1) short `brightness+`, (2) hold
`brightness+` ~3 seconds, (3) hold `brightness−` ~3 seconds. Capture within a
limited controlled window to avoid collecting unrelated RF identifiers.

## Handling a validation failure

1. Make **one change at a time**, compare against last known-good flashed YAML.
2. Recheck actual RX/TX pins, SPI, CC1101 Chip ID, and mode transitions.
3. Collect only the minimal relevant logs and observed physical behaviour.
4. Revert instead of stacking unverified changes.
