# Issue labels

Issue labels are a small, orthogonal taxonomy. Definitions (name, color and
description) live in [`.github/labels.json`](../.github/labels.json);
the [sync workflow](../.github/workflows/sync-labels.yml) creates/updates them
when the definition changes on `main` or when run manually.

| Dimension | Labels | Rules |
|---|---|---|
| Type | GitHub defaults: `bug`, `enhancement`, `documentation` | Usually exactly one; keep existing defaults rather than duplicating `type:*` |
| Priority | `priority:p0` through `priority:p3` | Exactly one for actionable issues; P0 is an immediate blocker or decision gate, not just an interesting idea |
| Area | `area:gateway`, `area:protocol`, `area:hardware`, `area:home-assistant`, `area:tooling` | One primary, optionally a second for cross-cutting work |
| Workflow | `status:blocked`, `needs:hardware`, `needs:decision` | **Temporary**, remove when resolved; can coexist when genuinely applicable |
| Hierarchy | `kind:epic` | Tracking/coordination issues; epics do not need a priority unless the epic itself represents an active decision gate |

## Priority meanings

- **P0:** Must happen before the next dependent milestone or unblocks an
  architectural/safety decision. A backlog should have only a few P0s.
- **P1:** Next implementation/reliability work, but not on the immediate
  critical path.
- **P2:** Useful planned work after the core gateway is stable.
- **P3:** Optional improvements with no urgent dependency.

The priority *label* is authoritative for triage. Priority prose in older issue
descriptions may reflect the time of issue creation. Update both if re-scoping a
specific issue substantially.

## Dependency and blocker conventions

- Link actual dependencies with GitHub's native **blocked by / blocking**
  relationships when available; otherwise use explicit `Blocked by: #123`
  references in issue bodies. **Do not create one label per dependency.**
- Use `status:blocked` only when useful work cannot progress *now* because
  a named external condition/issue is unresolved. Remove it as soon as the
  blocker clears; a normal prerequisite listed for a later acceptance test is
  not necessarily a current blocker.
- Use `needs:hardware` when actual on-device evidence is the **next required
  action**, not simply because an eventual feature will need a smoke test.
- Use `needs:decision` where the platform or design owner is unresolved
  (currently the HA-vs-ESPHome decision tracked in #27).
- A GitHub PR number, issue state (open/closed), assignee, or GitHub Project
  status is **not** duplicated by another permanent label.
- The taxonomy intentionally does not auto-apply or auto-remove issue labels:
  priority and blocker decisions require context and review.

## Current architecture gate

- [#22](../issues/22) is the shared RF gateway architecture epic.
- [#23](../issues/23), [#24](../issues/24) and [#25](../issues/25) gather
  experimental evidence.
- [#27](../issues/27) records the architecture decision.
- [#16](../issues/16) and [PR #21](../pull/21) are conditional ESPHome-native
  implementation work, not the already-decided final architecture.

The sync workflow **only manages label metadata**; it never closes issues,
changes their assignments or removes unspecified labels.
