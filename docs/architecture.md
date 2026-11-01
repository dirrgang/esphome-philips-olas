# Architecture

## Present architecture

```
Original Olas remote  ──433 MHz──▶ CC1101 ──RMT/SPI──▶ ESP32-S2 ──API──▶ Home Assistant
Home Assistant         ──API──────▶ ESP32-S2 ──RMT/SPI──▶ CC1101 ──433 MHz──▶ Fan
```

- The receiver scans raw marks/spaces for a valid frame; the pure helper
  `esphome/include/olas_protocol.h` checks ID, checksum and framing.
- Fan and binary-light `on_state`, the white-preset select's `set_action`,
  and direction/sleep/timer buttons' `on_press`
  invoke the same synchronous `olas_send` script. The portable `send_command`
  template uses the existing transmitter's TransmitCall API, encodes 41 bits,
  submits four identical copies, and decrements the shared 2-bit sequence once.
- RX `publish_state()` can trigger `on_state`; a scoped `olas_rf_update` guard
  prevents echo. Light RX publishes remote/current values directly. Its template
  binary output is inert because output writes occur later in the light loop,
  beyond the synchronous guard. Only synchronous publication requests RF.
- `olas_ready` enables Olas sends after component setup (`on_boot: -100`). Both
  fan restore publication and light startup publication are suppressed for RF.
  Fan speed/off mapping and last nonzero speed retention remain unchanged.
- Accepted remote frames update command-derived HA estimates without RF echo.
  Direction, sleep and timer actions use the same sequence and receive guards;
  see the [state model](#state-estimates-and-home-assistant-actions).
- Passive learning uses the same RX callback with an opt-in 60-second window;
  unknown frames remain candidates, never automatic pairings or TX triggers.
- Per-installation overlays and artifacts live under ignored `esphome/local/`;
  public code and synthetic host fixtures are independent of those files.
- Each profile resolves its own secrets and passes substitutions to the same
  shared gateway package. That package never performs a `!secret` lookup.
  Profile-local target files select IP/OTA or serial deployment independently
  of the firmware configuration. Builds are isolated inside each profile.

## Why code is separated this way

- The C++ header is a **pure transport-independent protocol codec**: host
  unit-testable and shareable with an eventual external component.
- The ESPHome YAML remains thin glue to existing CC1101 and native components.
- We deliberately avoid copying upstream's ESP8266 driver or web UI because
  ESPHome already manages Wi-Fi, OTA, API, CC1101 and GPIOs.

Architecture proposals and implementation sequencing are tracked in
[GitHub Issues](https://github.com/dirrgang/esphome-philips-olas/issues).
This document describes the implementation in this checkout.

## State estimates and Home Assistant actions

**No observed Olas RF acknowledgement or independent physical-state telemetry
exists.** HA power, speed and light values are estimates based on commands
sent or decoded, not measurements. A local TX request, API completion, RF
reception and independent physical effect are different evidence levels. See
[protocol](protocol.md) for device commands and
[verification](verification.md) for reproducible tests.

- **Fan power/speed:** deliberate HA OFF retains the last nonzero speed.
  When the ESP gateway was unplugged while the physical fan kept running, its
  restarted HA entity showed OFF. This is a proven **estimate divergence**,
  not an observed physical OFF or a boot-time RF transmission. Cached ON
  restoration is also unreliable if the remote acts while the gateway is
  offline. Startup/restore RF requests are guarded in software, but an
  independent boot-time RF measurement has not been supplied. Reconciliation
  policy is open in [#14](https://github.com/dirrgang/esphome-philips-olas/issues/14).
- **Light:** binary power starts as an assumed OFF UI state; the white
  preset starts unknown. A preset does not automatically set power. Missing
  RF frames or reconnects may invalidate both estimates. Absolute brightness
  and continuous Kelvin values are not exposed.
- **Direction:** `0x50` is a stateless toggle, not a measured forward/reverse
  trait; the direction may be unknown after missed frames.
- **Sleep:** `0x30` physically starts a stopped fan in the tested setup. A
  local accepted request or validated remote command publishes an estimated
  **fan ON** while retaining the prior speed (including zero after boot).
  The speed value is not a measurement of the dynamic sleep behavior.
  A scoped guard prevents a second fan-speed/OFF RF send on publication.
  The final merged behavior still needs an HA/hardware regression
  ([#12](https://github.com/dirrgang/esphome-philips-olas/issues/12)).
- **Timers:** 1 h, 3 h and 6 h remain stateless actions; no active timer,
  remaining time, cancel/replacement rule or expiry feedback is established.
  Physical expiry may leave the prior power estimate stale.

The native **Aktion** `template` event entity provides ten event types:
`direction_requested/received`, `sleep_requested/received` and
`timer_1h/3h/6h_requested/received`. A `requested` event records a local
accepted sender request; `received` records validated, deduplicated RF.
Neither confirms delivery, a mode state or physical effect. Events are
guarded against echo, blocked sends emit no requested event, and events
missed during HA disconnection are not replayed. Buttons carry no persistent
action state. Actual HA event rendering in the final
[PR #32](https://github.com/dirrgang/esphome-philips-olas/pull/32)
firmware remains unverified.

Current firmware controls **one RF address per private gateway profile**.
Two physical fans with different ID/check values were independently
controlled at separate installations. One gateway controlling multiple
physical fans simultaneously has not been tested or deployed. Host-only
A → B → A sequence-isolation tests are not physical multi-device support.
See [#20](https://github.com/dirrgang/esphome-philips-olas/issues/20)
and the pending [ADR #27](https://github.com/dirrgang/esphome-philips-olas/issues/27).

## Light representation and transport limits

ESPHome **2026.10.1** offers a `binary` light with `ON_OFF` traits and an
inert `template` binary output. A native select represents the three
discrete white presets. Without feedback, continuous Kelvin/absolute brightness
traits would misrepresent the device. `ALWAYS_OFF` is only the light's startup
UI assumption; the preset starts unknown. See [state model](#state-estimates-and-home-assistant-actions).

Pinned component sources:
[binary light](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/binary/light/binary_light_output.h),
[LightState](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/light/light_state.cpp)
and [template fan](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/template/fan/template_fan.cpp).
The native Olas path and generic RF proxy use the **same** CC1101 sender;
its transport and reply-ownership limitations are documented in the next
section. Generic proxy sends bypass the Olas sequence estimator; observe a
fresh remote frame before resuming native control.

## Shared-transmitter limits (ESPHome 2026.10.1)

The native API dispatches RF requests synchronously through
[`APIConnection::on_infrared_rf_transmit_raw_timings_request`](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/api/api_connection.cpp)
to `RadioFrequencyCall::perform()`, `RfProxy::control()` and
[`IrRfEntity::transmit_raw_()`](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/ir_rf_base/ir_rf_base.cpp).
That path and `olas::send_command()` both use the same `olas_tx` TransmitCall.
On ESP32/IDF >=5.5.1, `RemoteTransmitterBase::send_()` flushes the previous
completion before assigning the new transport sequence and entering
`send_internal()`. The RMT implementation waits for all previous symbols before
clearing/reusing its encoded buffer. Its driver queue depth is one. This
serializes consecutive normal main-loop submissions, including submissions from
different API connections; it provides neither an application FIFO nor a mutex
for arbitrary tasks/ISRs. Separate connections are serviced independently:
host submission timestamps do not establish a global RF send order. Encoding
uses shared mutable storage: never retain a
TransmitCall across asynchronous work or send reentrantly from a TX/completion
hook. A custom Olas-only queue would not arbitrate the generic proxy path.

Failure and completion meanings differ:

- [`fire_complete_(sent)`](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/remote_transmitter/remote_transmitter.h)
  notifies the proxy API before invoking the YAML `on_complete` hook. The hook
  runs on failure too and receives no success argument. It requests CC1101 RX;
  it proves neither successful transmission nor successful RX entry.
- The RMT backend reports `sent=false` for a failed component, failed carrier
  configuration, empty encoded data, failed `rmt_transmit`, or failed wait.
  A wait uses an unbounded driver timeout (`-1`); a hung driver can stall the
  main loop, so the proxy reply deadline is not a radio recovery watchdog.
- [`CC1101Component::begin_tx()` and `begin_rx()`](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/cc1101/cc1101.cpp)
  attempt IDLE followed by calibrated TX/RX. They return `void` and log a failed
  transition. RMT can therefore report success even if the radio never entered
  TX, and the completion hook can leave RX unrecovered after a radio failure.
  A fresh independently received remote command is needed to demonstrate RX.
- The proxy has one pending API reply slot per entity. A request from another
  connection can displace the previous request's reply with failure even though
  its waveform is already being transmitted or its completion is still pending.
  This can happen when each connection individually waits for its own replies:
  aioesphomeapi's transmit pacing is per connection, not shared across clients.
  Client disconnect clears the reply slot; it
  does not cancel an in-flight RMT transmission. Pace each proxy client by its
  completion and coordinate ownership across all clients. Do not interpret a
  negative reply as proof of no RF emission or automatically retry it.

The proxy advertises fixed 434 MHz/OOK. In
[`RfProxy::control()`](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/ir_rf_proxy/ir_rf_proxy.cpp)
the requested RF frequency/modulation do not configure the CC1101, and the
[`RadioFrequencyCall`](https://github.com/esphome/esphome/blob/2026.10.1/esphome/components/radio_frequency/radio_frequency.h)
does not enforce its advertised traits before calling `control()`. With this
gateway's hooks an out-of-band request can therefore radiate at **434 MHz**;
it cannot be used to prove retuning. Restrict clients to the advertised fixed
frequency/OOK. Do not add retuning to `on_control`: that callback runs before
the transmitter flushes the previous send and could retune an active waveform.

Recommendation: retain upstream waveform serialization for bounded normal-loop
tests, with a single coordinated owner for proxy submissions. Overlapping
independent clients to the same proxy entity are blocked by unreliable completion
ownership; execution evidence is in
[#24](https://github.com/dirrgang/esphome-philips-olas/issues/24).
An elapsed delay alone is not a completion guarantee. Keep production mixed-client
use gated on #24. A fix must either preserve the previous reply until its own
completion and serialize new admission, or refuse the new request before it
transmits while retaining the previous reply. Merely queuing native Olas sends
cannot fix the proxy's reply ownership. There is no guarantee of radio recovery
on every hardware/driver failure.

## Receive batching and duplicate suppression

The current native gateway processes the first valid Olas frame in an RMT
raw batch and suppresses an **exact** repeated frame for roughly 400 ms.
That is a heuristic for multiple copies of one physical press, not a
reliable boundary for rapid presses or held buttons. The alternative
receive-only multi-device scanner in
[PR #21](https://github.com/dirrgang/esphome-philips-olas/pull/21)
is not deployed.

The original remote's rapid ON → OFF failure cannot be explained by gateway
deduplication alone because the fan itself remained ON. The observed
device-level effect is documented in
[protocol](protocol.md#device-behavior-and-open-protocol-questions);
controller-side receive improvements are tracked in
[#6](https://github.com/dirrgang/esphome-philips-olas/issues/6)
and [#7](https://github.com/dirrgang/esphome-philips-olas/issues/7).
A two-bit sequence counter is not authentication or delivery feedback.
