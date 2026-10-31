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
| Light on | `0x7C` | Native TX and remote/HA sync user-confirmed; proxy TX also verified |
| Light off | `0xBC` | Native TX and remote/HA sync user-confirmed |
| Warm white | `0x6C` | Native TX and remote/HA sync user-confirmed |
| **Neutral white** | **`0x64`** | **Native TX and remote/HA sync user-confirmed** |
| Daylight white | `0x84` | Native TX and remote/HA sync user-confirmed |
| Brightness up | `0x70` | RX only |
| Brightness down | `0x28` | RX only |
| Reverse direction | `0x50` | Native TX and remote synchronization user-confirmed; toggle only |
| Sleep / night mode | `0x30` | User-confirmed starts stopped fan via HA and remote; speed pattern/exit unknown |
| Timer 1 h | `0x1C` | RX only |
| Timer 3 h | `0x18` | RX only |
| Timer 6 h | `0x14` | RX only |

**Note:** The upstream command table does not yet include neutral white `0x64`.

The gateway sends light power and the three white presets through the same
41-bit/four-copy path and sequence as the fan. Preset RX updates the discrete
selection only; its effect on power is not inferred. Encoding/host validation
does not establish physical TX success. On 2026-10-31 the user confirmed successful
installation and physical testing of native light ON/OFF, all three white presets
and original-remote synchronization on the test controller with PR #31's
implementation (`30819f4`). This is user-observed evidence, separate from device
logs and host checks; it does not establish a second device or mixed-client
arbitration. See [#8](https://github.com/dirrgang/esphome-philips-olas/issues/8)
and [PR #31](https://github.com/dirrgang/esphome-philips-olas/pull/31).

## Stateless direction, sleep and timer actions

Five template buttons submit `0x50`, `0x30`, `0x1C`, `0x18` and `0x14` through
the same sender, check XOR, four-copy waveform and sequence as fan/light. Their
RX path validates ID/check, retains the existing duplicate filter, synchronizes
the next shared sequence, logs the command and emits a `*_received` action event.
It does not press the HA button,
echo RF or change fan/light estimates. A deliberate repeated button press submits
a new action with a new sequence; no automatic retry is added.

- Direction `0x50` is a toggle, with no confirmed forward/reverse state.
- Sleep `0x30` starts the stopped fan via HA and original remote (user-confirmed
  on one test controller). The user describes dynamically varying speed based on
  prior experience; the pattern has not been measured. Toggle/repeat semantics,
  exit conditions and actual speeds remain unknown. No active sleep state or
  inferred fixed speed is published; the previous fan estimate can be stale.
- Timer commands are labelled 1 h / 3 h / 6 h from captured remote functions.
  Whether selections reset, toggle, cancel or replace timers is unknown. There
  is no countdown, automatic cancellation or assumed HA fan-OFF at expiry.

Direction TX and physical reversal, plus original-remote synchronization, were
user-confirmed on one test controller for PR #32 (`6e17499`) on 2026-10-31.
Device logs show native `0x50` seq 3 and later remote `0x50` seq 2; no following
TX request appears in the supplied excerpt. This is not an independent on-air
measurement or absolute direction feedback. Sleep startup from OFF is now
user-observed via both local TX and original remote, separately from speed-pattern
characterization. All three timers remain **physically unverified for activation
and expiry**; a native 1 h TX-request log is not physical confirmation. Sleep operation and timer
expiry may invalidate the retained fan estimate without an observable RF event.
Evidence and semantic questions belong to [#11](https://github.com/dirrgang/esphome-philips-olas/issues/11),
[#12](https://github.com/dirrgang/esphome-philips-olas/issues/12) and
[#13](https://github.com/dirrgang/esphome-philips-olas/issues/13).

### Action events

The native `template` event entity reports `direction_requested/received`,
`sleep_requested/received` and `timer_1h_requested/received`,
`timer_3h_requested/received`, `timer_6h_requested/received` (ten separate types).
Local events follow accepted shared-sender requests; RX events follow the same
ID/check and duplicate filtering as the control path. The scoped RX guard also
covers synchronous event callbacks, preventing a callback from retransmitting.
There is no event for blocked sends or for fan/light/unrecognized commands.
Existing bounded exact-frame dedup remains a heuristic; distinct new sequences
are accepted. Events carry no device IDs, raw captures or inferred remaining time.

HA displays the event time and type, and the API log formatter prints `[S][event]`.
This records command activity, not RF delivery or an active timer/sleep state.
The pinned native API does not replay old events on initial subscription/reconnect;
events missed while HA is offline are not reconstructed. HA may retain an old
event timestamp as history. See the [HA event model](https://www.home-assistant.io/integrations/event/)
and [HA ESPHome event platform](https://github.com/home-assistant/core/blob/2026.10.4/homeassistant/components/esphome/event.py).

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
