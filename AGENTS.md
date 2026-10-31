# Project instructions

## Context and tracking

- This is an independent ESPHome gateway using official CC1101, remote RX/TX
  and native entity components. Keep the protocol codec portable and host-testable.
- Use [GitHub Issues and PRs](https://github.com/dirrgang/esphome-philips-olas/issues)
  for work items, priorities, dependencies, acceptance progress and test results.
  Read the relevant issue/PR before related work; reuse existing issues.
  Do not maintain local backlogs, status/handoff files or current-task lists.
- Consult `README.md` for setup, `docs/protocol.md` for codec changes,
  `docs/architecture.md` for component boundaries and `docs/verification.md`
  for firmware/hardware verification. Update technical docs when behaviour changes.

## Checks

Use the pinned mise toolchain (`mise install` to provision it):

- Codec changes: `mise run test` (Bash and C++17; Zig on Windows).
- Reference YAML changes: `mise run config`, then `mise run compile` with
  ignored `esphome/secrets.yaml` present.
- Private hardware profiles: `mise run config-local`, then `mise run compile-local`.
  `mise run init-local` creates the ignored workspace without replacing files.
- Docs-only changes need link/consistency checks, not a firmware build.

## Hardware and evidence

- Keep real profiles, credentials, exports and captures in ignored `esphome/local/`;
  public templates and tests must work without any real installation.
- Compilation is separate from deployment. Before an authorized flash, compare
  with the actual installed configuration and preserve Wi-Fi/API/OTA access.
- CC1101 requires 3.3 V and a suitable 433 MHz antenna before transmitting.
- Separate host/build checks, device logs and user-observed physical results
  in the relevant GitHub issue/PR. Never claim testing that was not performed.
- Check XOR remains configurable; do not promote an unproven universal rule.
- Preserve RX-to-TX guarding and legitimate repeated presses. HA state describes
  commanded/overheard state, not confirmed telemetry; do not invent absolute
  brightness or direction feedback.
- Review attribution/license requirements before reusing upstream source.
  Do not push or open upstream PRs without the user's approval.
