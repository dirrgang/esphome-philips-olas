# Olas RF protocol: observations, not guesses

## Evidence and provenance

- Initial independent project: https://github.com/kercle/philips-olas-remote
  (MIT-licensed ESP8266/CC1101 controller, most timing and opcode research).
- Additional on-device ESPHome CC1101 captures: **2026-10-30**.
- **Two different physical Olas units in separate installations** have been
  successfully controlled with individually configured 24-bit RF IDs and
  check-XOR constants. Both independently observed ID/check pairs agree with
  the ID-byte-XOR derivation described below. They were not operated
  simultaneously by one gateway; see [#20](https://github.com/dirrgang/esphome-philips-olas/issues/20).
- The full local capture was decoded and the resulting device accepted our
  generated RF command. No over-the-air acknowledgement has been observed.

## Physical coding

- CC1101 carrier: approximately 434 MHz, ASK/OOK, carrier on = positive mark.
- Sync: mark ~7,400 µs; space ~1,090 µs.
- Bit 0: mark ~340 µs, space ~730 µs.
- Bit 1: mark ~730 µs, space ~340 µs.
- Each logical bit lasts ~1,070 µs.
- 41 bits, most significant bit first; ~52.4 ms per frame.
- An ordinary key press transmits multiple **identical** frames (roughly 4–6 as
  documented upstream; received batches captured ~4 complete frames).
  Do not infer the behavior of a **held** key from one decoded command: RMT
  batching and the 400 ms exact-frame deduplication heuristic can hide activity.
  Held brightness and white-tone buttons still require separate raw RF captures
  ([#9](https://github.com/dirrgang/esphome-philips-olas/issues/9)).

## Frame layout

```
[24-bit fan ID][6-bit function][2-bit sequence][8-bit check][0]
bits 40–17    16–11           10–9        8–1           0
```

- Sequence cycles downward modulo 4: 2 → 1 → 0 → 3 → 2.
- All frame copies during one physical press use the same sequence.
- The 8-bit command includes the 6-bit function and 2-bit sequence;
  the check is calculated from the **entire command byte**, including sequence.
- The per-device check XOR constant differs between independently observed
  remotes. Its derivation is explained below.
- The same command and sequence field is used for fan, light, and special
  function codes. Per-device TX/RX sequence bookkeeping is an implementation
  responsibility; see [architecture](architecture.md).
- This protocol is not authenticated or encrypted. Do not publish captures of
  personal remotes if their IDs matter to your threat model.

### Check byte and per-device XOR constant

For a 24-bit remote ID `0xAABBCC`, the following rule matches **both
independently observed physical devices** (with different IDs and constants):

```text
check_xor = AA XOR BB XOR CC
command   = (function & 0xFC) | (sequence & 0x03)
check     = command XOR check_xor
```

For example, using a **synthetic** ID `0x123456`:

```text
check_xor = 0x12 XOR 0x34 XOR 0x56 = 0x70
command   = 0x40 | 0x02           = 0x42  (speed 1, sequence 2)
check     = 0x42 XOR 0x70         = 0x32
```

**Evidence and confidence:** two separate real Olas fans were controlled with
different individually observed ID/check pairs; each check-XOR constant equals
the XOR of that ID's three bytes. This supports the derivation as a practical
candidate for other devices, not as a proven universal invariant across all
Olas hardware revisions. The upstream firmware uses a fixed `0x5B` constant;
whether its device's ID also produces `0x5B` has not been independently
checked here. Keep `check_xor` explicitly configurable as an override, validate
the candidate against multiple commands and sequences from each target remote,
and do not assume its acceptance without a physical test.

Receive-only discovery can parse complete structural candidates without an
expected ID/check. Its `xor_candidate = command XOR received_check` is only an
observation from that frame, not an integrity check or pairing result. Compare
the candidate with the ID-byte XOR, and verify a stable constant across
different commands and sequences before enabling TX. Discovery results are
recorded in [#15](https://github.com/dirrgang/esphome-philips-olas/issues/15);
the two-device/same-gateway distinction is tracked in [#20](https://github.com/dirrgang/esphome-philips-olas/issues/20).
Keep real RF IDs and raw captures private.

## Verified local command codes

Function codes are masked with `command & 0xFC` (sequence bits zeroed).

| Function | Hex | Validation |
|---|---|---|
| Fan off | `0x10` | RX and TX physically tested |
| Fan speed 1 | `0x40` | RX and TX physically tested |
| Fan speed 2 | `0xAC` | RX and TX physically tested |
| Fan speed 3 | `0x9C` | RX and TX physically tested |
| Fan speed 4 | `0x20` | RX and TX physically tested |
| Fan speed 5 | `0x80` | RX and TX physically tested |
| Fan speed 6 | `0x8C` | RX and TX physically tested |
| Light on | `0x7C` | Native TX and remote/HA sync user-confirmed; proxy TX also verified |
| Light off | `0xBC` | Native TX and remote/HA sync user-confirmed |
| Warm white | `0x6C` | Native TX and remote/HA sync user-confirmed |
| **Neutral white** | **`0x64`** | **Native TX and remote/HA sync user-confirmed** |
| Daylight white | `0x84` | Native TX and remote/HA sync user-confirmed |
| Brightness up | `0x70` | Remote RX identified; held-button signaling unresolved |
| Brightness down | `0x28` | Remote RX identified; held-button signaling unresolved |
| Reverse direction | `0x50` | Native TX and physical toggle plus remote RX confirmed; no absolute direction telemetry |
| Sleep / night mode | `0x30` | Physically starts stopped fan via native TX and original remote; speed pattern/exit unknown |
| Timer 1 h | `0x1C` | Remote RX and native TX **request** logged; physical activation/expiry unverified |
| Timer 3 h | `0x18` | Remote RX observed; native physical activation/expiry unverified |
| Timer 6 h | `0x14` | Remote RX observed; native physical activation/expiry unverified |

**Upstream difference:** Neutral white `0x64` is absent from the
[original upstream command table](https://github.com/kercle/philips-olas-remote);
it was independently captured and physically exercised here. The table's
validation column distinguishes physical response from receive-only decoding
and local TX submission. Detailed physical test evidence stays in the relevant
[issues and PRs](https://github.com/dirrgang/esphome-philips-olas/issues).

## Device behavior and open protocol questions

The following statements concern the **physical fan/light or received RF**,
not Home Assistant state estimates or event entities. Device-level behavior
must be distinguished from a decoded command, a local transmission request and
an actual over-air RF measurement.

- **Light white tones:** warm `0x6C`, neutral `0x64` and daylight `0x84`
  select three physically verified discrete presets. Whether a preset also
  changes light power independently has not been established. Continuous
  brightness or Kelvin position is not observable in this protocol.
- **Direction `0x50`:** a physical **toggle**, with a successful native
  command and original-remote operation observed on one unit. There is no
  absolute forward/reverse status or ACK; operating-condition effects remain
  incompletely characterized ([#11](https://github.com/dirrgang/esphome-philips-olas/issues/11)).
- **Sleep `0x30`:** physically starts a stopped fan when triggered from
  either native RF control or the original remote. A varying speed pattern was
  described but not measured; repeat/toggle behavior and exit conditions remain
  unknown ([#12](https://github.com/dirrgang/esphome-philips-olas/issues/12)).
- **Timers `0x1C` / `0x18` / `0x14`:** received from the original remote
  for 1 h / 3 h / 6 h selections. A local 1 h TX **request** was logged, not
  independently measured on air. Actual timer activation, replacement,
  cancellation and expiry have not been physically established
  ([#13](https://github.com/dirrgang/esphome-philips-olas/issues/13)).
- **Rapid remote ON → OFF:** one real test left the fan physically ON, and the
  gateway observed only the ON action. It is unknown whether the remote emitted
  a valid OFF frame, the fan rejected it, or timing/air reception caused the
  failure. Since the remote operates the fan directly, a gateway-only RX/dedup
  defect cannot explain the physical outcome. This is an *open RF/receiver
  behavior investigation*, not an established new opcode or frame rule
  ([#7](https://github.com/dirrgang/esphome-philips-olas/issues/7)).
- **Held buttons:** short versus held brightness `0x70` / `0x28` (and
  potential sustained white-tone controls) have not been characterized at
  raw RF level. Seeing one application-level command is insufficient because
  RX batching and duplicate filtering can hide repeated frames
  ([#9](https://github.com/dirrgang/esphome-philips-olas/issues/9)).

The state model, HA events, startup behavior and CC1101/RF-proxy arbitration
belong to [architecture](architecture.md). Reproducible capture and behavioral
tests belong to [verification](verification.md); individual measured results
remain in issues/PRs.

## Synthetic examples

- **ID-derived check:** synthetic ID `0x123456`, check XOR `0x70`,
  speed 1, sequence 2: `command=0x42`, `check=0x32`.
- **Explicit override / codec fixture:** synthetic ID `0x654321`,
  configured check XOR `0xA7`, speed 1, sequence 0: `command=0x40`,
  `check=0xE7`, packed frame `0xCA864281CE`. This deliberately does
  **not** follow the ID-byte-XOR hypothesis; it tests independently
  configurable check values, not a third physical device.
- These values are synthetic; real identifiers and captures remain private.

