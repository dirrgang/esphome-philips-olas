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

### Generic RF proxy: user-confirmed on flashed firmware (2026-10-31)

- Encrypted ESPHome API handshake succeeded.
- Existing Olas `on_raw` lambda decodes the original remote and updates the
  ESPHome fan state; this is distinct from generic proxy reception.
- Native API advertises the generic RF receiver; an independent Python client
  receives raw RF through `aioesphomeapi.subscribe_infrared_rf_receive()`.
- Speeds 1, 2 and 3 each yielded four valid Olas frames from 385 timing values;
  observed sequence progression was `1 → 0 → 3`.
- These are user-reported results from before the repository diagnostic CLI.
  Generic proxy TX, physical fan actuation via that path, and subsequent RX
  return remain **unverified**. The previous inline TX attempt failed with
  stdin `EOFError` before transmission, not an RF failure.

### Reproducible diagnostic (host-tested, not hardware-tested)

`scripts/rf_proxy_diagnose.py` provides bounded receive-only diagnosis and an
explicitly enabled, terminal-confirmed speed-1 → speed-2 proxy TX experiment.
Host tests use synthetic frames and fake clients only. See
[verification.md](verification.md#independent-rf-proxy-diagnostic--issue-23).
This supports [#23](https://github.com/dirrgang/esphome-philips-olas/issues/23)
and the still-open architecture gate
[#27](https://github.com/dirrgang/esphome-philips-olas/issues/27); it creates no
permanent competing protocol/entity implementation or HA integration.
Firmware compilation, CLI device smoke tests and cross-client arbitration
remain separate checks; no OTA or RF transmission was performed for this work.
On 2026-10-31, `mise install`, C++ tests and 16 Python host tests passed, as did
the uv/mise CLI help checks. `mise run config` and `mise run compile` also
succeeded with ESPHome 2026.10.1 against the **pre-existing locally modified
YAML**, using local secrets. That YAML is excluded from the diagnostic change;
this build does not validate the committed reconstruction or flashed revision.

## Reconstructed code needing validation

- `esphome/include/olas_protocol.h`: pure C++17 unit-tested codec (new).
- `esphome/olas-rf-gateway.yaml`: faithful *example* based on prior working
  snippets, now using this header and secrets-based substitutions. The combined
  refactor has **not been compiled with ESPHome and not yet flashed**.
- Do not describe the refactor as deployed, or assume an ESPHome config check
  succeeded just because `scripts/test.sh` passes.

## Pending / unresolved

1. Run `esphome config`, then `esphome compile`, against ESPHome 2026.10.1.
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
9. Consider packaging as an ESPHome external component only after baseline
   regression tests pass; avoid introducing a large rewrite in one PR.
10. Share neutral-white opcode upstream and ask about differing check XOR.

## Known limitations

- No RF acknowledgement or true device-state telemetry. HA reflects *commanded
  or overheard state*, not independently verified physical state.
- RMT capture can end after about 4 whole frames + part of fifth. One valid
  frame suffices for RX decoding.
- Decoder currently accepts the first matching frame in a batch; multiple
  dissimilar frames bundled together are not fully handled.
- Duplicate suppression `same frame within 400 ms` is heuristic. The 2-bit
  sequence wraps every 4 commands and must not be used as authentication.
- The observed XOR constant differs from original project's `0x5B`. The
  conjecture `check = ID_byte0 XOR ID_byte1 XOR ID_byte2 XOR cmd` fits this
  remote but is not yet proven across different device IDs.
- Remote sequences were not perfectly consecutive in one log interval; lost
  transmissions or intervening presses are plausible explanations.

## Acceptance criteria for the next PR

- No regression in fan speed 1–6 and off via both HA and physical remote.
- Correct deduplication and no reflected RF transmission from RX events.
- Host protocol tests pass (`./scripts/test.sh`).
- ESPHome 2026.10.1 config and firmware compilation succeed on the user's setup.
- Actual physical smoke test is recorded; do not substitute compilation for it.
- New evidence clearly separated from assumptions in docs.
