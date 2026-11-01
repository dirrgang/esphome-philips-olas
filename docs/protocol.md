# Olas RF protocol: observations, not guesses

## Evidence and provenance

- Initial independent project: https://github.com/kercle/philips-olas-remote
  (MIT-licensed ESP8266/CC1101 controller, most timing and opcode research).
- Additional on-device ESPHome CC1101 captures: **2026-10-30**.
- **Two different physical Olas units in separate installations** have been
  successfully controlled with individually configured 24-bit RF IDs and
  check-XOR constants. Both independently observed ID/check pairs agree with
  the ID-byte-XOR derivation described below. They were not operated
  simultaneously by one gateway; see [#20](../issues/20).
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
  ([#9](../issues/9)).

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
- The currently deployed single-device sender uses one descending sequence
  across fan, light and action commands. Synthetic A → B → A tests demonstrate
  independently tracked counters for two addresses, including wraparound
  ([PR #34](../pull/34)); they do **not** establish deployed multi-device
  firmware or one gateway controlling two physical fans.
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
the two-device/same-gateway distinction is tracked in [#20](../issues/20).
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

Merged [PR #32](../pull/32) adds five stateless template buttons submitting
`0x50`, `0x30`, `0x1C`, `0x18` and `0x14` through the same sender, check XOR,
four-copy waveform and sequence as fan/light. Their
RX path validates ID/check, retains the existing duplicate filter, synchronizes
the next shared sequence, logs the command and emits a `*_received` action event.
It does not press the HA button,
echo RF or infer active mode/timer state. Sleep is the power-estimate exception:
accepted local or remote `0x30` publishes fan ON under the no-send guard.
Direction/timer actions leave power estimates unchanged. A deliberate repeated button press submits
a new action with a new sequence; no automatic retry is added.

- Direction `0x50` is a toggle, with no confirmed forward/reverse state.
- Sleep `0x30` starts the stopped fan via HA and original remote (user-confirmed
  on one test controller). The user describes dynamically varying speed based on
  prior experience; the pattern has not been measured. Toggle/repeat semantics,
  exit conditions and actual speeds remain unknown. No active sleep state or
  inferred fixed speed is published. Fan power is optimistically ON after a
  sent or accepted remote sleep command, matching observed startup from OFF. The
  speed field is retained, including zero/unknown after boot, and can be stale
  during the dynamic pattern. Publication never sends a second speed command.
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
and expiry**. Remote RX logs confirm all three timer codes; a native 1 h TX
request followed those captures with the expected next sequence. This is not
proof of an over-air transmission or timer activation. Sleep operation and
timer expiry may invalidate the retained fan estimate without an observable RF
event. The final merged PR #32 action-event display and sleep-triggered ON
estimate were compiled/host-tested but have not been independently validated
on that final firmware in the supplied hardware evidence.
Evidence and semantic questions belong to [#11](https://github.com/dirrgang/esphome-philips-olas/issues/11),
[#12](https://github.com/dirrgang/esphome-philips-olas/issues/12) and
[#13](https://github.com/dirrgang/esphome-philips-olas/issues/13).

### Action events

Merged PR #32's native `template` event entity reports `direction_requested/received`,
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
Physical HA event rendering on the final PR #32 firmware remains unverified;
an expected event is not equivalent to a confirmed device action.
The pinned native API does not replay old events on initial subscription/reconnect;
events missed while HA is offline are not reconstructed. HA may retain an old
event timestamp as history. See the [HA event model](https://www.home-assistant.io/integrations/event/)
and [HA ESPHome event platform](https://github.com/home-assistant/core/blob/2026.10.4/homeassistant/components/esphome/event.py).

## Observed rapid-command and restart limitations

- **Rapid original-remote ON → OFF:** In one physical test the fan started and
  remained ON; the controller also received only the ON action. Thus this is
  **not merely an HA display or event issue**. Whether the original remote
  emitted a valid OFF frame, the fan rejected it, or RF timing/sequence and
  receiver limitations caused the loss is unknown. The remote operates the fan
  directly: controller-side dedup alone cannot explain the physical result.
  A timestamped raw capture and varying key-press intervals are needed
  ([#7](../issues/7), [#6](../issues/6)). Do not assert that OFF was never sent.
- **ESP gateway power cycle while the fan remains running:** The fan continued
  physically ON, while the restarted controller/HA reported OFF. That is an
  **optimistic state-estimation mismatch**, not evidence of a fan power-off
  or RF command on boot. Normal deliberate HA OFF correctly retains the last
  nonzero speed; that is a different behavior. No RF ACK or physical-state
  telemetry is available to reconstruct missed remote actions
  ([#14](../issues/14), [#3](../issues/3)).
- **Long-press behavior:** Brightness-up/down and any sustained WW/DW
  color-temperature adjustment remain uncharacterized at raw RF level.
  Neither one logged command nor an ordinary button opcode proves the held
  waveform or implies a truthful absolute brightness/kelvin value
  ([#9](../issues/9), [#10](../issues/10)).

## RF transport and proxy boundaries (not Olas frame semantics)

Merged [PR #34](../pull/34) and hardware tests in [#24](../issues/24)
establish that normal sequential native Olas/proxy sends can work through one
CC1101, but **independent RF-proxy clients are not safely coordinated**:
two clients targeting the same proxy entity displaced the earlier completion
reply (A=`false`, B=`true` in three bounded trials), even when each client
paced its own submissions. A negative proxy result does **not** prove that no
RF was emitted, so it must not trigger an automatic retry. Use one coordinated
proxy transmit owner until reply ownership is corrected.

The CC1101 remains configured for fixed **434 MHz ASK/OOK**; RF-proxy
frequency metadata does not dynamically retune the radio. RMT completion,
CC1101 TX/RX-mode transition, a decoded remote command, and the fan's actual
physical response are different evidence levels. No Olas over-air ACK exists.
See [architecture/transport limitations](architecture.md#shared-transmitter-limits-esphome-2026101)
and [verification](verification.md#mixed-client-stress-procedure--24);
these limitations are **transport/integration issues**, not an unknown
41-bit Olas protocol field.

## Synthetic examples

- **ID-derived check:** synthetic ID `0x123456`, check XOR `0x70`,
  speed 1, sequence 2: `command=0x42`, `check=0x32`.
- **Explicit override / codec fixture:** synthetic ID `0x654321`,
  configured check XOR `0xA7`, speed 1, sequence 0: `command=0x40`,
  `check=0xE7`, packed frame `0xCA864281CE`. This deliberately does
  **not** follow the ID-byte-XOR hypothesis; it tests independently
  configurable check values, not a third physical device.
- These values are synthetic; real identifiers and captures remain private.

## Interpreting an ESPHome RAW log

An `on_raw` batch can contain four complete frames plus a partial fifth.
Positive intervals are RF-on (mark); negative intervals RF-off (space).
Example (simplified): `7346,-1093,709,-340,337,-736,...` means sync,
bit `1`, bit `0`, etc. Ignore noise and nonmatching 433 MHz devices.

For a long press, the application-level decoder's 400 ms duplicate window and
RMT batching can hide RF activity. Further raw-batch timing experiments are
needed; do not infer the transmitter sends only once from a single command log.
