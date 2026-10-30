# Instructions for Codex and coding agents

**Source of truth and current state:** read `README.md`, `docs/STATUS.md`,
`docs/protocol.md`, and `docs/verification.md` before implementing anything.

## Authority and evidence

- Verified user hardware behaviour in `docs/STATUS.md` is authoritative.
- `esphome/olas-rf-gateway.yaml` is a reconstructed reference, **not** the
  proven currently flashed file. Ask for / compare the live config before OTA.
- Never claim physical testing or ESPHome compilation unless actually run.
- Keep facts, hypotheses and intended behaviour explicitly separate.
- Review the upstream MIT license and attribution if reusing any upstream code.

## Scope

- Target ESPHome 2026.10.1, ESP32-S2 Mini, CC1101 ASK/OOK at 434 MHz.
- Prefer official ESPHome CC1101 + remote RX/TX + native entities.
- Keep protocol codec portable and host-testable; avoid hardcoding one check
  constant as the universal Philips Olas rule.
- Do not implement a giant rewrite, web server, standalone firmware or
  unnecessary dependencies without a specific reason.

## Workflow

1. Understand existing behaviour and document testable acceptance criteria.
2. Make **one coherent small change**.
3. Use the project's mise toolchain: `mise install`, `mise run test`, and,
   once `esphome/secrets.yaml` exists, `mise run config` then `mise run compile`.
   The underlying commands are `scripts/test.sh` and `esphome config/compile`.
4. Ask for real-hardware smoke tests and record results as observed/unverified.
5. Update `docs/STATUS.md` and `docs/protocol.md` on relevant discoveries.
6. Prefer small PRs. Never push or open an upstream PR without user's approval.

## Safety and robustness

- CC1101 must be 3.3 V; no transmitting without a suitable 433 MHz antenna.
- Avoid exposing Wi-Fi credentials, HA API/OTA credentials, or unnecessary
  device-specific RF captures. `secrets.yaml` is gitignored.
- No HA state may pretend to be independently confirmed physical telemetry.
- Prevent RX-to-TX loops; do not lose legitimate repeated presses.
- Do not mark dimming or absolute direction as working yet.

## Next task to pick up

Validate the refactored YAML (configuration, compilation and device smoke test)
**before** adding lighting. Then investigate held brightness buttons as a
separate diagnostic task, followed by small features with corresponding tests.
