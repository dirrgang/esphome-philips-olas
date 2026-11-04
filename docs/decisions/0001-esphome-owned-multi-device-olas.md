# ADR 0001: ESPHome-owned multi-device Olas component

- **Status:** Accepted
- **Date:** 2026-11-04
- **Decision issue:** [#27](https://github.com/dirrgang/esphome-philips-olas/issues/27)
- **Implementation:** [#16](https://github.com/dirrgang/esphome-philips-olas/issues/16)

This ADR sets the target architecture. **It is not implemented yet**, and it
authorizes no firmware migration or RF transmission. The
[architecture document](../architecture.md) describes the implementation in the
current checkout.

## Context

The owner wants to control two real Olas fans from one Home Assistant instance,
preferably through one gateway, to publish the result, and to be able to use
the same radio for other RF devices later.

The shipped firmware controls one RF address per profile. Its limit is the
project's own glue, not the ESPHome radio stack: `olas-rf-gateway.yaml` holds
one fixed address, one global sequence, one duplicate filter and one set of
entities.

Evidence used, read from source on 2026-11-04 unless stated otherwise:

- ESPHome's `remote_receiver` calls every registered listener. Its
  `ir_rf_proxy` entity does not consume received data. The in-tree `climate_ir`
  platform combines `RemoteReceiverListener` and `RemoteTransmittable` in one
  entity on a shared receiver and transmitter.
- Stock Home Assistant 2026.10.4 and the 2026.11.0b5 beta have no ESPHome RF
  receiver entity. It exists on `dev` (`2026.12.0.dev0`), merged in
  [home-assistant/core#189484](https://github.com/home-assistant/core/pull/189484).
- The Home Assistant RF integrations inspected (`novy_cooker_hood`,
  `klik_aan_klik_uit`) only transmit, and a code search found no consumer of
  the receiver helper. The search was not exhaustive.
- ESPHome's fan API has a boolean `state` and no `assumed_state`.
- The proxy's single reply slot behaves as described in
  [architecture](../architecture.md#shared-transmitter-limits-esphome-2026101),
  with logic unchanged in ESPHome 2026.10.1, 2026.10.2 and `dev`.
- Two real fans were controlled independently at separate installations. One
  gateway reaching both has not been tested
  ([#20](https://github.com/dirrgang/esphome-philips-olas/issues/20)).

No test of the target design has been performed.

## Decision

Olas moves into an ESPHome external component that supports multiple logical
devices on one shared CC1101 transmitter and receiver.

| Concern | Owner |
| --- | --- |
| CC1101 configuration and TX/RX switching | Official ESPHome `cc1101` and `remote_transmitter` hooks |
| Waveform serialization | ESPHome `remote_transmitter`; no Olas-only TX queue |
| Olas codec | Portable, host-tested header without framework dependency |
| Receive scanning and routing | One component listener per receiver |
| Sequence, duplicate filter, state estimates | One independent instance per logical device |
| Fan, light, white preset, action and event entities | The component, exposed through the ESPHome native API |
| Generic RF for other devices | Optional `ir_rf_proxy`, or further ESPHome components |

Rules for the component:

- **Shared radio:** the listener never consumes received data, so proxy and
  other listeners keep working. Frames for unknown addresses are ignored, not
  treated as errors or used to create pairings.
- **Per-device configuration:** every device has its own address and an
  explicit check XOR. The ID-byte XOR stays a candidate, not a universal rule.
- **Duplicate addresses** are rejected at configuration time.
- **Receive path:** process every valid frame in a batch and apply duplicate
  handling per device. Press-boundary policy stays with
  [#6](https://github.com/dirrgang/esphome-philips-olas/issues/6) and
  [#7](https://github.com/dirrgang/esphome-philips-olas/issues/7).
- **Guards:** keep startup send suppression and RX-to-TX echo protection, per
  device, and keep legitimate repeated presses.
- **Existing features:** six speeds and off, light on/off, three white presets,
  stateless direction, sleep and timer actions, events and passive learning
  must all survive the migration.

**Gateways:** use one gateway where RF coverage allows. Where it does not, use
a further gateway and configure each fan on exactly one gateway.

**Proxy policy:** one coordinated proxy client per gateway, normally Home
Assistant. Do not overlap independent clients on a proxy entity and do not
automatically retry a negative reply. This policy is sufficient for the current
requirements, so no ESPHome reply-ownership fix is required. Native component
sends do not use the proxy reply slot, but they do share the transmitter.

**State:** values remain estimates from commands sent or decoded. No startup or
reconnect RF is sent, and no absolute direction, brightness or timer state is
invented. The native fan cannot show unknown or assumed state, so the exact
restart behavior within these limits stays with
[#14](https://github.com/dirrgang/esphome-philips-olas/issues/14) and must be
documented where it is implemented.

**PR #21:** do not merge it. Reuse its address routing, schema and tests
selectively in new, smaller changes on top of current `main`.

## Alternatives considered

- **Home Assistant-owned Olas over a generic proxy.** Rejected for now. It offers
  per-device setup through a config flow, no recompile for a new fan, and honest
  unknown or assumed state. However, the receive path is unreleased, no inspected
  integration uses it, a Home Assistant restart would lose frames, raw RF would
  have to reach Home Assistant, and the tested logic and part of the physical
  regression would have to be redone.
- **Duplicated YAML per fan.** Rejected. It would copy address-specific lambdas
  and globals into every profile instead of giving each device a clear instance.
- **One gateway per fan with today's firmware.** Works without new code and
  stays the fallback if coverage or the migration fails, but it needs more
  hardware than necessary where one radio reaches both fans.
- **Split ownership in firmware and Home Assistant.** Rejected, because two
  long-term Olas stacks would compete for sequence and state.

## Consequences

- Two fans can share one radio without waiting for a Home Assistant release, if
  coverage allows.
- Decoding stays local, so the gateway keeps following the remote while Home
  Assistant restarts and raw RF does not have to reach Home Assistant.
- Other RF devices can later be added as further listeners and transmitters or
  through the proxy. They remain limited to what the fixed 434 MHz OOK radio
  configuration can reach, and they share air time with Olas. Receiver settings
  such as `idle` and `filter` apply to all listeners.
- Adding a fan needs a configuration change and a firmware update.
- [#14](https://github.com/dirrgang/esphome-philips-olas/issues/14) can only be
  mitigated, not fully solved, while the native fan has no unknown state.
- The project depends on ESPHome's internal component APIs. Pin the tested
  version and validate upgrades in CI.
- Distribution is an ESPHome external component with a firmware template, not a
  HACS integration.

## Migration and rollback

1. Keep the current firmware and a known-good OTA image until the component
   reaches feature parity.
2. Build the component with synthetic multi-device tests first, including
   A → B → A isolation and negative cases.
3. Validate a one-device configuration against the
   [physical regression procedures](../verification.md#physical-regression-matrix)
   before adding a second device.
4. Preserve or explicitly document Home Assistant entity identity changes.
5. Add the second fan once coverage is established. Report one real device,
   synthetic addresses and two real devices on one radio as separate evidence.

Rollback is an OTA update to the retained image. Every flash and RF test needs
its own authorization under the [project rules](../../AGENTS.md#hardware-and-evidence).

## Revisit triggers

- Home Assistant ships the ESPHome receiver in a stable release and real
  integrations use it reliably, **and** configuration through the UI or honest
  unknown state becomes more important than local decoding.
- [#14](https://github.com/dirrgang/esphome-philips-olas/issues/14) cannot be
  mitigated acceptably with native entities.
- ESPHome removes or materially restricts shared listeners or transmitter use.
- Concurrent independent proxy clients become a real requirement.
- Physical tests show that one shared radio cannot control two fans reliably.
