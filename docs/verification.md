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
Python tests also cover the diagnostic codec, discovered entity identity,
capability rejection, terminal/confirmation guards, connection cleanup and
four-frame TX request construction. All API clients in tests are fakes;
tests never connect to hardware or transmit RF. No Python API package is needed
for host tests; the CLI uses the pinned inline uv dependency instead.

## Independent RF-proxy diagnostic — Issue #23

This is a bounded experiment for [#23](https://github.com/dirrgang/esphome-philips-olas/issues/23),
feeding the architecture decision in [#27](https://github.com/dirrgang/esphome-philips-olas/issues/27).
It does not change firmware, flash OTA or create HA entities. The repository
YAML is not evidence of the actually flashed configuration. Record the actual
firmware/config revision separately, without credentials.

As documented in #27's compatibility finding, HA Core 2026.10.4's built-in
`radio_frequency` integration provides TX, not a generic RF receiver event feed.
An independent native-API client's RX result is distinct from HA's existing
lambda → native fan-state path. The RF-proxy API remains experimental; this
diagnostic does not settle protocol/entity ownership or cross-client arbitration.

### Receive only (default)

From the repository root, in a real interactive terminal:

```bash
mise install
mise run rf-diagnose -- 192.168.20.157 --receive-seconds 30
```

Equivalent direct invocation through the selected toolchain:

```bash
mise exec -- uv run --script scripts/rf_proxy_diagnose.py 192.168.20.157
```

The inline dependency is `aioesphomeapi==46.12.0`, matching ESPHome 2026.10.1.
uv manages an isolated cache; there is no global pip install or new project
environment. The host defaults to `192.168.20.157` (no mDNS requirement).
Enter the API encryption key at the hidden `getpass` prompt; it is never saved
or printed. Do not pipe source into Python: unlike the failed inline heredoc
experiment, this file leaves stdin available for terminal input.

Press the original remote's speed buttons. Output contains timing counts,
validated function, sequence and identical-frame counts, never complete raw
captures. Check that the event's API `(device_id, key)` matches the discovered
receiver; the tool filters on both. Distinguish this API device ID from the
24-bit Olas fan ID inside the RF frame.

Defaults `--fan-id 0xAA8C03 --check-xor 0x25` describe the observed local remote,
not a universal Olas rule. Override both for another verified pairing. Unknown
functions, wrong ID/check, malformed timings and partial frames are rejected.
All complete valid frames are decoded, including repeated and mixed frames.
`copies` counts identical frames in a batch; `repeat<400ms` is only a diagnostic
heuristic across batches. Neither suppresses incoming frames or causes TX.

The tool selects exactly one API-advertised RF receiver supporting 434 MHz and
OOK. TX mode separately requires an equally suitable transmitter. If ambiguous,
rerun with the listed `--receiver DEVICE_ID:OBJECT_ID` and/or
`--transmitter DEVICE_ID:OBJECT_ID`. Keys are obtained only from discovery.
Missing frequency/modulation metadata fails closed; compare live configuration
instead of guessing keys or assuming an advertised frequency tunes the CC1101.

### Controlled manual transmit test (not executed by agents or CI)

Use only with the CC1101 powered at 3.3 V and a suitable 433 MHz antenna.
Keep other HA/ESPHome transmitters idle throughout; this script cannot arbitrate
between independent clients. Have the real light visible for observation.

**Exact manual command:**

```bash
mise run rf-diagnose -- 192.168.20.157 --tx-light-on --post-rx --receive-seconds 30
```

1. Enter the encryption key, then press **LIGHT OFF on the original remote** when
   instructed. Confirm the light is physically off. No baseline means no TX.
2. The tool validates the received frame, takes its sequence and computes
   `(sequence + 3) & 3` (downward modulo 4, including `0 → 3`). It encodes LIGHT ON
   (`0x7C`), checks 41-bit MSB-first round-trip decoding and alternating
   positive marks / negative spaces. It prepares 84 timing values per frame.
3. Review the summary, then type **SEND** within 30 seconds immediately before
   transmission. Any other answer, EOF, missing terminal, connection loss or
   intervening different Olas command aborts. There is no unattended bypass.
4. Exactly one `radio_frequency_transmit_raw_timings()` request uses the
   discovered transmitter's key and API device ID, `frequency=434000000`,
   `modulation=OOK`, and `repeat_count=4`: **four total identical frames**, not
   four additional repeats. Timings contain one frame only; no extra gap is
   invented. Approximate total waveform duration is 209 ms.
5. **Observe the real light turning on.** A successful Python API call
   only means the request was handed to the client, not that RF was emitted,
   completed, or physically accepted. The connection remains open beyond the
   nominal waveform duration; the tool does not claim a completion ACK.
6. With `--post-rx`, press Enter when ready and then **LIGHT OFF on the remote**.
   A fresh light-off frame on the same receiver demonstrates post-TX RX delivery;
   a light-on echo alone is not sufficient. Record the physical light response
   separately; existing fan-state updates still require their own regression test.
   Without `--post-rx`, this step is skipped.

API operations default to 10-second timeouts (`--timeout`); controlled RX windows
use `--receive-seconds`. Cancellation closes the subscription and connection.
No automatic reconnect or TX retry occurs: after uncertain delivery, inspect
physical state and start a new test with a fresh light-off baseline.

### Evidence ledger (2026-10-31)

| Check | Evidence / result |
|---|---|
| Encrypted API handshake on flashed device | User-confirmed before this CLI |
| Existing `on_raw` → ESPHome fan state | User-confirmed on flashed device |
| Proxy receiver → independent `aioesphomeapi` RX | User-confirmed before this CLI |
| Speed 1 / 2 / 3 decoding | User-confirmed: four valid frames per 385 timings, sequences `1 → 0 → 3` |
| Earlier inline TX failure | `EOFError` from heredoc-occupied stdin before transmission; not RF failure |
| Light diagnostic hardware acceptance | Light off via remote → on via proxy → off via remote; pending, no light TX claimed verified |
| Diagnostic host tests | Synthetic codec fixtures and fake API only; see host test command above |
| Host validation during implementation | `mise install`, `mise run test` (C++ + 16 Python tests), direct uv and mise `--help`, and `git diff --check` passed |
| Local ESPHome config / compile | Both tasks passed with ESPHome 2026.10.1 against the already locally modified YAML and local secrets; that YAML is excluded from this diagnostic change. Does not validate the committed reconstruction or flashed revision |
| This CLI against the real gateway | Pending manual RX test |
| Generic proxy TX / physical light-on reaction | Pending manual test above |
| CC1101 return to RX after proxy TX | Pending fresh remote light-off test |
| Cross-client serialization / native Olas regression | Still pending (#23/#24) |

Record date, actual firmware revision, discovered receiver/transmitter identity,
function/sequence/copy counts, physical response, fresh post-TX RX result and
legacy fan-state/no-echo result. Do not commit keys or full private RF captures.

## ESPHome validation/compile (committed reconstruction still needs validation)

Run with the ESPHome 2026.10.1 CLI installed by mise; these commands do not
flash the device.

```bash
cp esphome/secrets.yaml.example esphome/secrets.yaml
# EDIT THE FILE: replace Wi-Fi, HA API and OTA credentials.
mise run config
mise run compile
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
