# Project instructions

## Context and tracking

- This is an independent ESPHome gateway using official CC1101, remote RX/TX
  and native entity components. Keep the protocol codec portable and host-testable.
- GitHub Issues own **current open work**, priorities, blockers and acceptance
  criteria. PRs own changes, reviews and evidence for those changes. Read the
  relevant issue/PR first and reuse existing issues.
- Do not maintain local backlogs, status/handoff files or current-task lists.
  Follow the knowledge ownership rules below before editing documentation.

## Knowledge ownership and change routing

Each fact has **one canonical home**. Other places may summarize only what
their audience needs and link to that home; do not maintain parallel technical
specifications, command tables, test histories or project-status summaries.

| Canonical location | Owns | Does not own |
| --- | --- | --- |
| Production source in `esphome/`, `scripts/` and `tests/` | What is implemented and what executable tests assert | Proof of physical behavior from compilation alone |
| `docs/protocol.md` | Olas RF physical/frame format, opcode meanings, ID/check/sequence derivation, experimentally established device behavior, clearly labeled unresolved **protocol** hypotheses | HA entity/UI behavior, ESPHome transport concurrency, runtime lifecycle or test execution history |
| `docs/architecture.md` | Implemented component boundaries, protocol/transport ownership, state estimation, API/CC1101/RMT constraints and design consequences | Duplicate opcode tables, experiment logs or proposed decisions represented as accepted |
| `docs/decisions/*.md` (when present) | **Accepted** architecture decisions (ADR), context, alternatives, rationale and revisit triggers | Open deliberation or running progress reports; proposals stay in their Issue |
| `docs/verification.md` | Reproducible test procedures, evidence standards, acceptance matrices and safe execution constraints | Chronological test results or evergreen status updates |
| `docs/hardware.md` | Hardware requirements, pinout and wiring | Protocol/control behavior or project status |
| `docs/upstream.md` | Third-party provenance, attribution, license obligations and contribution boundaries | Copies of upstream protocol tables or contribution progress |
| `README.md` | User-facing overview, setup, configuration, operation and a **short** limitations summary linking to details | Detailed research, design justifications, status logs or alternate specifications |
| `.github/labels.json` | Machine-readable label definitions (synced to GitHub) | Workflow explanations; `docs/issue-labels.md` owns usage guidance |
| GitHub Issue body | **Current** problem/decision, remaining unknowns, tasks, dependencies, acceptance criteria and links to canonical documentation | Repeated specifications, accumulated historical status updates or superseded conclusions |
| GitHub Issue comments / PR discussion | Dated evidence, reproduction details, observations, corrections and review history | Current canonical behavior or an independently maintained summary |
| GitHub PR | Proposed change, rationale, diff, validation evidence, review and merge history | Permanent project status or an alternative specification to be maintained after merge |
| `AGENTS.md` | Contribution workflow, evidence rules and the ownership policy itself | Duplicated technical content or a task tracker |
| Ignored `esphome/local/` | Installation-specific secrets, actual RF addresses, raw captures and private hardware logs | Public repository artifacts |

### Evidence and update workflow

1. Record a new physical observation or test result in the relevant Issue
   comment or PR, distinguishing **measured physical effect**, RX capture,
   TX request/completion, synthetic/host test and unverified interpretation.
   Keep real IDs, credentials, raw captures and personal installation details
   out of public content; refer to private evidence without copying it.
2. Once a conclusion is sufficiently supported, update **only its canonical
   document**. Clearly state scope, evidence strength and remaining hypotheses.
   A proposal is not an adopted ADR; a test submission is not RF delivery;
   optimistic HA state is not physical telemetry.
3. Update the related Issue **body in place** to reflect only current work,
   replacing obsolete status/claims instead of appending another dated status
   section. Preserve the chronology in comments, PRs and Git history. Close
   completed issues when acceptance criteria are met; link canonical docs.
4. Keep the README concise and link to the owner document when user-facing
   operation or safety limitations change. Cross-reference other domains
   instead of copying their detailed findings.
5. Before closing a PR, check that its changed behavior is represented in the
   correct canonical location, relevant Issue bodies are current, links work,
   and earlier contradictory claims have been superseded or removed.

**When sources disagree:** identify the *type* of claim first. Source code
shows implemented logic; tests/CI show only what their fixtures actually
exercised; device observations establish physical effects within the tested
scope; canonical docs state established protocol and intended design; Issues
state remaining work; PRs/comments retain history. Do not silently choose the
newest statement or mistake a merged PR for a physical test. Reconcile the
conflict at the appropriate canonical home and update the affected current
Issue. Do not edit historical PR discussions merely to make them read as current.

## Checks

Use the pinned mise toolchain (`mise install` to provision it):

- Codec changes: `mise run test` (Bash and C++17; Zig on Windows).
- Shared YAML changes: `mise run config`, then `mise run compile` use the
  synthetic fixture. Never install that fixture or require real credentials.
- Profile/tooling changes: `mise run test-profiles`. Secrets must resolve in
  the selected private profile, never in the shared package.
- Private profiles: `config-local` / `compile-local -- --profile <name>`;
  `init-local` creates a profile without replacing files. `install-local`
  builds then uploads via its selected `--target` (OTA/IP or serial).
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
