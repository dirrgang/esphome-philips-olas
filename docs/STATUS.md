# Project state (2026-10-31)

This is the **authoritative handoff** for subsequent Codex work. Read this
before modifying any firmware. Actual ESPHome device configuration may have
small differences from the reconstructed example; always compare with the
currently flashed YAML before attempting OTA changes.

## Validated on physical hardware

- ESP32-S2 Mini, 433 MHz CC1101 module, external antenna, 3.3 V power.
- ESPHome 2026.10.1; CC1101 identified as `0x0017`; RX at 434 MHz, ASK/OOK.
- Remote frames correctly decoded with device ID `0xAA8C03` and per-device
  observed command XOR constant `0x25`.
- Original remote: speed 1–6 and fan off reliably update native HA fan entity.
- HA: send speed 1–6 and fan off successfully controls real fan.
- RX guard prevents an inbound remote command triggering another RF send.
- State while off retains last nonzero speed (expected HA fan behaviour).
- Independent captures identified additional light and timer command codes.

## Locally validated code (not physical verification)

- `components/olas/olas_protocol.h`: canonical pure C++17 codec;
  `esphome/include/olas_protocol.h` forwards to it for host compatibility.
- `esphome/olas-rf-gateway.yaml`: faithful *example* based on prior working
  snippets, using the canonical header and secrets-based substitutions. On
  2026-10-31, `mise run config` and `mise run compile` both passed with ESPHome
  2026.10.1 / ESP-IDF 5.5.6 / ESP32-S2. This independently repeats the successful
  user build reported in [#2](https://github.com/dirrgang/esphome-philips-olas/issues/2).
- First step of [#16](https://github.com/dirrgang/esphome-philips-olas/issues/16):
  typed external-component schema, one RX listener, shared decoder dispatch and
  per-device sequence/dedup/observed fan state. Two **synthetic** address/check
  pairs pass host tests and schema validation. The ESP32-S2 fixture compiles.
- The component has **no native fan/light platform and no RF send path yet**.
  Its transmitter reference is reserved and typed, without mode changes.
- No firmware was flashed, no live device config was compared and no physical
  regression or independent two-fan control was tested during this work.
  Hardware migration remains gated by #3 and #20.

## Pending / unresolved

1. Config/compile completed locally; retain these checks for subsequent changes.
2. Compare against the *actual* flashed YAML; keep its Wi-Fi/API/OTA keys.
3. Flash incremental refactor, verify all speed/off RX and TX transitions.
4. Measure long press of brightness+/−: Does holding emit repeated bursts,
   one long continuous transmission, or another code? Log raw batch counts
   **temporarily** without recording unrelated neighborhood transmitters.
5. Add lighting on/off and 3 presets only after independent TX tests.
6. Design dimming without fabricating reliable absolute-brightness feedback.
7. Direction uses a toggle code, not an absolute direction command; do not
   claim synchronized direction without evidence.
8. Handle power-cycles/reboots, lost packets and two-remote edge cases.
9. Continue the required external-component architecture in small PRs. Next:
   completion-driven serialized TX plus native fan binding, with transport tests.
   Deployment/migration requires baseline regression (#3) and separate physical
   addresses/checks for both fans (#20); synthetic tests cannot satisfy these.
10. Share neutral-white opcode upstream and ask about differing check XOR.

## Known limitations

- No RF acknowledgement or true device-state telemetry. HA reflects *commanded
  or overheard state*, not independently verified physical state.
- RMT capture can end after about 4 whole frames + part of fifth. One valid
  frame suffices for RX decoding.
- Legacy YAML still accepts the first matching frame in a batch. The new core
  dispatches all complete frames to registered addresses; mixed-device batches
  have host coverage only, without measurements of actual RMT batching.
- Duplicate suppression `same frame within 400 ms` is heuristic. The 2-bit
  sequence wraps every 4 commands and must not be used as authentication.
- The observed XOR constant differs from original project's `0x5B`. The
  conjecture `check = ID_byte0 XOR ID_byte1 XOR ID_byte2 XOR cmd` fits this
  remote but is not yet proven across different device IDs.
- Remote sequences were not perfectly consecutive in one log interval; lost
  transmissions or intervening presses are plausible explanations.

## Acceptance criteria for the first #16 core PR

- One shared raw listener and portable codec; no per-device pulse re-decoding.
- Two distinct address/check pairs route independently; unknown/malformed frames
  leave state untouched; counters and duplicate filters are per-device.
- Empty/malformed devices, duplicate addresses/IDs and invalid transport ID
  references fail ESPHome configuration validation.
- Host and schema tests pass; both reference and core fixture config/compile.
- Existing YAML controls remain available; no entity or hardware migration.

## Physical acceptance criteria (still pending, #3 / #20)

- No regression in fan speed 1–6 and off via both HA and physical remote.
- Correct deduplication and no reflected RF transmission from RX events.
- Host protocol tests pass (`./scripts/test.sh`).
- ESPHome 2026.10.1 config and firmware compilation succeed on the user's setup.
- Actual physical smoke test is recorded; do not substitute compilation for it.
- New evidence clearly separated from assumptions in docs.
