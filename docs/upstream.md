# Upstream contribution strategy

Original project: https://github.com/kercle/philips-olas-remote

The original standalone ESP8266/PlatformIO controller is MIT licensed. It is
not the same deployment architecture as this ESPHome gateway. We recommend
**independent ESPHome project + narrowly scoped upstream PRs**, not maintaining
an entire feature-divergent fork.

## Potential first upstream PR

1. Add the independently observed neutral-white function code `0x64` to the
   command enum, parser switch and documented command table (if appropriate).
2. Add a clear note that a second remote was observed to use
   `check = command XOR 0x25`, not the upstream fixed `0x5B`.
3. Propose allowing device-specific check constants, with tests for both
   variants. **Do not** claim that ID-byte-XOR is a general rule without
   captures for two distinct device IDs and the upstream author's confirmation.
4. Include short reproducible sanitized trace excerpts, a methodology and
   explicit distinction between on-air observations and proposed abstraction.

The upstream author should be asked whether they want ESPHome integration
linked in the README, rather than receiving a huge PR changing their
standalone firmware into an ESPHome project.

## Attribution

This repository's original code was written independently based on public
protocol documentation and follow-up measurements. If code is later copied
from upstream, preserve their 2026 MIT copyright and license text as required.

## Suggested issue message (draft, not posted)

> I integrated a Philips Olas into ESPHome using an ESP32-S2 and CC1101, based
> on your excellent protocol analysis. A second device produces valid commands
> with a different check XOR constant (`0x25` rather than `0x5B`), and I
> captured an additional neutral-white command (`0x64`). I have a focused set
> of traces/tests and could prepare a small PR documenting the extra command
> and making the check constant configurable. Would that fit your project?
