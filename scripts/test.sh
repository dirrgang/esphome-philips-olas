#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -I"$ROOT/esphome/include" \
  "$ROOT/tests/test_protocol.cpp" -o "$TMP/olas-tests"
"$TMP/olas-tests"
