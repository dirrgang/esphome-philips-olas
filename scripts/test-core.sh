#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# ESPHome copies include headers to a different build location. Keep both
# self-contained copies identical until the ADR decides canonical ownership.
cmp -s "$ROOT/esphome/include/olas_protocol.h" "$ROOT/components/olas/olas_protocol.h" || {
  echo "Divergent Olas codecs: sync the main and optional component copies" >&2
  exit 1
}
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
if command -v c++ >/dev/null 2>&1; then
  COMPILER=(c++)
elif command -v zig >/dev/null 2>&1; then
  COMPILER=(zig c++)
else
  echo "C++ compiler not found" >&2
  exit 1
fi
"${COMPILER[@]}" -std=c++17 -Wall -Wextra -Werror -pedantic -I"$ROOT/components/olas" \
  "$ROOT/tests/test_core.cpp" -o "$TMP/olas-core-tests"
"$TMP/olas-core-tests"
