#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
if command -v c++ >/dev/null 2>&1; then
  COMPILER=(c++)
elif command -v zig >/dev/null 2>&1; then
  COMPILER=(zig c++)
else
  echo "C++ compiler not found. On Windows, run mise install; on Unix, install GCC/Clang." >&2
  exit 1
fi
"${COMPILER[@]}" -std=c++17 -Wall -Wextra -Werror -pedantic -I"$ROOT/esphome/include" \
  "$ROOT/tests/test_protocol.cpp" -o "$TMP/olas-tests"
"$TMP/olas-tests"

# ESPHome copies these headers to different build locations, and private profiles
# name the esphome/include path. Keep both copies identical until the single-device
# gateway is retired (ADR 0001).
cmp -s "$ROOT/esphome/include/olas_protocol.h" "$ROOT/components/olas/olas_protocol.h" || {
  echo "Divergent Olas codecs: sync esphome/include and components/olas" >&2
  exit 1
}
"${COMPILER[@]}" -std=c++17 -Wall -Wextra -Werror -pedantic -I"$ROOT/components/olas" \
  "$ROOT/tests/test_core.cpp" -o "$TMP/olas-core-tests"
"$TMP/olas-core-tests"

# Synthetic RF proxy diagnostics; API network calls are fully mocked.
python3 -m unittest discover -s "$ROOT/tests" -p 'test_rf_proxy_diagnose.py' -v
