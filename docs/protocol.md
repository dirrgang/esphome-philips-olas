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
- Observed local device ID: `0xAA8C03`.
- Observed local integrity check: `check = command XOR 0x25`.
- Upstream firmware currently implements `command XOR 0x5B` for its device.
- **Unproven hypothesis:** local `0x25` = `0xAA XOR 0x8C XOR 0x03`, so the
  check might be derived from all ID bytes. More than one real pairing is
  required to distinguish this from device-specific fixed constants.
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
| Light on | `0x7C` | RX only |
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

## Examples from local captures

- `ID=AA8C03`, speed 1, `seq=0`: `command=0x40`, `check=0x65`.
- `ID=AA8C03`, speed 2, `seq=1`: `command=0xAD`, `check=0x88`.
- The test transmitter successfully sent speed 1 using the local checksum.

## Interpreting an ESPHome RAW log

An `on_raw` batch can contain four complete frames plus a partial fifth.
Positive intervals are RF-on (mark); negative intervals RF-off (space).
Example (simplified): `7346,-1093,709,-340,337,-736,...` means sync,
bit `1`, bit `0`, etc. Ignore noise and nonmatching 433 MHz devices.

For a long press, the application-level decoder's 400 ms duplicate window and
RMT batching can hide RF activity. Further raw-batch timing experiments are
needed; do not infer the transmitter sends only once from a single command log.

## Software-only multi-device coverage (2026-10-31)

The new external-component core scans a batch once, extracts each structurally
valid 41-bit frame, selects a configured device by the 24-bit address, then
checks that device's explicit `check_xor`. Unknown addresses, invalid checks
and malformed frames cannot update sequence or state. The legacy `decode_raw`
API still returns the first frame matching its caller's address/check pair.

Host fixtures use **synthetic**, deliberately independent pairs `0x123456/0xA7`
and `0x654321/0x39`. These are software routing vectors, not evidence for the
second physical remote, pairing, or any universal checksum rule (#15 / #20).

Each device retains the prototype's 400 ms exact-frame heuristic, measured from
the last accepted frame. A different sequence/function is accepted immediately;
copies do not extend the window. Millisecond counter wrap is tested. A genuine
press with exactly the same 41 bits inside the window remains ambiguous; held
buttons and real remote/HA interleaving still require #7 hardware evidence.
No new light, dimming, timer, direction or sleep behavior is implemented.
