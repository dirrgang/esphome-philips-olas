# Olas RF protocol: observations, not guesses

## Evidence and provenance

- Initial independent project: https://github.com/kercle/philips-olas-remote
  (MIT-licensed ESP8266/CC1101 controller, most timing and opcode research).
- Additional on-device ESPHome CC1101 captures: **2026-10-30**.
- The full local capture was decoded and the resulting device accepted our
  generated RF command. No over-the-air acknowledgement has been observed.

## Physical coding

- CC1101 carrier: approximately 434 MHz, ASK/OOK, carrier on = positive mark.
- Sync: mark ~7,400 µs; space ~1,090 µs.
- Bit 0: mark ~340 µs, space ~730 µs.
- Bit 1: mark ~730 µs, space ~340 µs.
- Each logical bit lasts ~1,070 µs.
- 41 bits, most significant bit first; ~52.4 ms per frame.
- A key press transmits multiple **identical** frames (roughly 4–6 as
  documented upstream; received batches captured ~4 complete frames).

## Frame layout

```
[24-bit fan ID][6-bit function][2-bit sequence][8-bit check][0]
bits 40–17    16–11           10–9        8–1           0
```

- Sequence cycles downward modulo 4: 2 → 1 → 0 → 3 → 2.
- All frame copies during one physical press use the same sequence.
- An independently observed remote used `check = command XOR 0x25`.
- Upstream firmware currently implements `command XOR 0x5B` for its device.
- **Unproven hypothesis:** XOR of the three ID bytes matched the observed
  constant for one remote. This does not establish a general derivation rule;
  keep the check constant configurable and verify each pairing independently.
- Receive-only discovery can parse complete structural candidates without an
  expected ID/check. Its `xor_candidate = command XOR received_check` is only
  an observation from that frame, not an integrity check or pairing result.
  Verify a consistent candidate across different commands and sequences for
  each intended remote before configuring control. Compatibility is per target.
- Discovery execution evidence is recorded in
  [GitHub issue #15](https://github.com/dirrgang/esphome-philips-olas/issues/15);
  a consistent capture does not establish a universal derivation rule.
- This protocol is not authenticated or encrypted. Do not publish captures of
  personal remotes if their IDs matter to your threat model.

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
| Light on | `0x7C` | RX and generic RF-proxy TX physically verified (2026-10-31; #23) |
| Light off | `0xBC` | RX only |
| Warm white | `0x6C` | RX only |
| **Neutral white** | **`0x64`** | **RX only, newly identified** |
| Daylight white | `0x84` | RX only |
| Brightness up | `0x70` | RX only |
| Brightness down | `0x28` | RX only |
| Reverse direction | `0x50` | RX only; toggling semantics |
| Sleep / night mode | `0x30` | RX only |
| Timer 1 h | `0x1C` | RX only |
| Timer 3 h | `0x18` | RX only |
| Timer 6 h | `0x14` | RX only |

**Note:** The upstream command table does not yet include neutral white `0x64`.

The gateway sends light power and the three white presets through the same
41-bit/four-copy path and sequence as the fan. Preset RX updates the discrete
selection only; its effect on power is not inferred. Encoding/host validation
does not establish physical TX success. Only LIGHT ON via the generic proxy
has physical light-TX evidence in [#8](https://github.com/dirrgang/esphome-philips-olas/issues/8).
Native light/preset TX still needs physical verification.

## Synthetic example

- Test fixture ID `0x654321`, check XOR `0xA7`, speed 1, sequence 0:
  `command=0x40`, `check=0xE7`, packed frame `0xCA864281CE`.
- These values are synthetic; real identifiers and captures remain private.

## Interpreting an ESPHome RAW log

An `on_raw` batch can contain four complete frames plus a partial fifth.
Positive intervals are RF-on (mark); negative intervals RF-off (space).
Example (simplified): `7346,-1093,709,-340,337,-736,...` means sync,
bit `1`, bit `0`, etc. Ignore noise and nonmatching 433 MHz devices.

For a long press, the application-level decoder's 400 ms duplicate window and
RMT batching can hide RF activity. Further raw-batch timing experiments are
needed; do not infer the transmitter sends only once from a single command log.
