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
Synthetic A → B → A sends also verify separate caller-owned counters across
wraparound with distinct IDs/checks and fixed waveform vectors. This does not
add multi-device support to the YAML or test RMT serialization.
The shared sender is exercised through a fake TransmitCall: all five light
commands, direction/sleep/three timer actions and six speeds/off,
independent MSB-first pulse/check expectations,
four identical copies, one shared descending sequence including wraparound,
invalid IDs/checks, startup/control-disabled gates and guarded RX callbacks
that cannot submit TX. This does not execute ESPHome on hardware.
Action-event tests cover all ten requested/received type mappings, exclusion of
fan/light/unknown codes, callback guarding and hidden event configuration in both
synthetic private profiles. Actual HA event display still needs integration testing.
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

## Automated firmware CI and offline smoke tests — #4

[GitHub Actions ESPHome firmware validation](../.github/workflows/esphome.yml)
runs for PRs/pushes changing firmware, the workflow, the fixture or its tooling,
and can be started manually. On Ubuntu 24.04 it installs the pinned
`mise.toml` tool versions, then runs:

```bash
mise run version
mise run config
mise run smoke-config
mise run compile
```

`config` and `compile` use only the committed synthetic
`esphome/validation.yaml` with dummy credentials and one synthetic RF ID.
The **negative smoke test** creates a temporary sibling of that fixture
with a deliberately missing gateway `!include`, verifies that ESPHome fails
*specifically* for that missing path, and deletes the temporary file.
It fails CI if validation accepts the broken include or fails for an unrelated
reason. It never opens the API, sends RF, flashes hardware, or touches private
`esphome/local/` profiles. A successful build proves configuration and
compilation, **not** ESP32/CC1101/HA runtime behavior.

Only the mise tool cache is provided by the action initially; full
PlatformIO/build caching is deferred until CI runtime is measured. The
mise/ESPHome versions are pinned in `mise.toml`, but the Python transitive
dependencies and PlatformIO toolchain are **not yet fully lockfile-pinned**.
Full physical fan/light/remote smoke testing remains a separate, manual
[#3](https://github.com/dirrgang/esphome-philips-olas/issues/3) procedure;
RF-proxy transmit arbitration remains [#24](https://github.com/dirrgang/esphome-philips-olas/issues/24).

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
CI and unattended agent runs must not flash or send RF; interactive tool-assisted
hardware work requires explicit user authorization. Record **RX logs**, **TX request/completion
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

## Mixed-client stress procedure — #24

Use the installed private configuration and retain a known-good OTA image before
any authorized firmware change. Explicitly authorized interactive hardware work
may use Codex tools. Keep bounded probes and captures in `esphome/local/`.
No synthetic fixture may be flashed. Request physical observations separately;
API state and completion are not device acknowledgements.

| Scenario | Submission and evidence | Stop condition |
|---|---|---|
| Paced native → proxy → native | Native speed 1, proxy LIGHT ON (four copies), native OFF; wait for each finish and fresh remote LIGHT OFF RX before proceeding | Missing RX, failure log, or unexpected physical effect |
| Short gaps | After paced pass, repeat at 250 ms, 50 ms and zero submission gap; record native request, proxy completion, TX/RX hooks and post-burst remote RX separately | Missing/extra hook, RMT error or RX loss |
| Independent clients | Two API connections, bounded alternating requests to the same transmitter; distinguish submission order from RF completion | Unmatched replies, persistent failure or RX loss |
| Proxy A → proxy B | Two independently paced connections to the same proxy entity; paired LIGHT ON requests at 50 ms, no added wait and 250 ms, settling between pairs, then a single-client LIGHT ON and fresh remote LIGHT OFF RX | Any displaced/negative completion prevents declaring overlapping proxy use reliable |
| Disconnect | Disconnect the proxy client after submission; observe the remaining waveform/completion and fresh remote RX using another connection | Do not label disconnect as cancellation |
| Invalid input | Empty timings/unknown entity first; separately inject driver/radio failures only in a recoverable test fixture | A refusal does not establish CC1101 hardware-failure recovery |
| Frequency | Review fixed 434 MHz/OOK metadata against source; use host vectors for mismatches | Do not radiate an out-of-band-labelled request on the fixed Olas channel |
| Devices A → B → A | Synthetic IDs/checks and independently wrapped counters first; retain separate per-device next-sequences | One physical fan cannot validate a second real device |

Run short-gap tests only after the paced physical sequence succeeds. If a native
command is logged but has no observed effect, reproduce it in isolation before
attributing the failure to contention. Record firmware compilation time/revision,
requested function/sequence, completion status, independent RX and physical
observations in #24. A post-test OFF request is cleanup, not proof the fan stopped.
See [the source-level limits](architecture.md#shared-transmitter-limits-esphome-2026101)
for unbounded RMT waits, radio transition failures and single-slot proxy replies.

For a native startup anomaly, first confirm physical standstill using remote OFF.
Send native speed 1 in isolation, observe for 25 seconds, then send native OFF.
Record function/sequence and actual startup/stop separately from the estimated
API state. Compare with remote speed 1/OFF under the same physical conditions,
then repeat the native trial after standstill. Do not insert proxy sends, change
the codec, or attribute a single missed start to sequence collision without
reproducing it. Keep the receive-only comparison capture bounded and distinguish
any later native commands that happen to fall inside that capture window.

For proxy/proxy tests, record each connection's reply independently. The stock
Python client paces each connection separately, so one outstanding request per
connection still permits overlapping requests to the shared proxy entity. Use
only known fixed-frequency LIGHT ON frames with four copies and explicit shared
sequence tracking; the three pairs plus final single-client request total seven
requests. A final physical ON/OFF check and fresh remote RX do not prove that
each of the seven frames was emitted. Do not automatically retry a displaced
negative reply or treat a fixed 250 ms delay as a completion guarantee.

## Manual stateless action tests

Use the reviewed private profile and preserve installed access/rollback as above.
These tests require deliberate user operation; builds and CI never send RF.
Record firmware revision, RX and TX-request/completion logs, and observed physical
effects separately in #11/#12/#13 and the PR, using neutral device labels.

| Action | RX / HA expectation | TX expectation | Physical observation to record |
|---|---|---|---|
| Direction toggle `0x50`, under manufacturer-permitted operating conditions | Remote press logged, sequence synchronized; no absolute direction | One shared-path request per deliberate HA press; no request from RX | Actual reversal and any stop/start behaviour; repeat only deliberately |
| Sleep `0x30` at several starting speeds, including OFF | `sleep_requested` or `sleep_received`; no assumed active mode or speed | Same shared path and four copies | Startup from OFF user-confirmed via HA/remote; pattern, repeat and speed/OFF exit still to characterize |
| Each timer `0x1C` / `0x18` / `0x14` | No active timer, remaining time or automatic fan-OFF estimate | Correct requested code and shared sequence | Initial response separately from a fully observed 1 h / 3 h / 6 h expiry |
| Successive timer choices, repeated same timer, then speed/OFF | No inferred cancellation or replacement | Each deliberate press is one action | Reset, replacement, toggle/cancel behaviour; mark unknown if not observed |
| Remote action → HA fan/light → remote action through wraparound | Sequence stays shared; no button replay or RX echo | Unchanged fan/light waveforms; no second sender | Device accepts alternating controls; all six speeds/off and light regressions |
| Restart and control-disabled profile | No restored action or invented mode state; buttons hidden with fan | No startup TX; disabled control rejects actions | No unintended action; retain original-remote operation |
| Each of five actions via HA and original remote | Exactly one matching `*_requested` / `*_received` event in HA and `[S][event]` API logs; distinct presses retained | RX event callback must not cause TX | Feedback is command activity, not acknowledgement |
| HA reconnect / gateway restart | No newly triggered/replayed action event; historical HA timestamp can remain | No startup or reconnect TX | Missed offline events are not reconstructed |

Direction reversal and remote synchronization have user-confirmed evidence in
#11/PR #32 for one test controller. Sleep startup from OFF is user-confirmed via
HA and remote in #12; varying speed is a user description, not a measured pattern.
Timer effects/expiry, sleep repeat/exit semantics, new event display, full
direction operating-condition checks and remaining regressions still need
the tests above. Do not infer a full timer timeout test from an immediate
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
