#!/usr/bin/env python3
"""Prove ESPHome config validation rejects a broken include (no hardware access)."""

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "esphome" / "validation.yaml"
VALID_INCLUDE = "gateway: !include olas-rf-gateway.yaml"
MISSING_INCLUDE = "gateway: !include __ci_missing_olas_gateway__.yaml"


def main() -> int:
    original = FIXTURE.read_text(encoding="utf-8")
    if original.count(VALID_INCLUDE) != 1:
        print("Validation fixture changed: expected exactly one gateway include", file=sys.stderr)
        return 1

    # Keep this temporary fixture next to validation.yaml so relative includes
    # resolve as they do in the real build. No real profiles or secrets are read.
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", suffix=".yaml",
        prefix=".ci-invalid-", dir=FIXTURE.parent, delete=False,
    ) as handle:
        invalid_path = Path(handle.name)
        handle.write(original.replace(VALID_INCLUDE, MISSING_INCLUDE, 1))

    try:
        result = subprocess.run(
            ["esphome", "config", str(invalid_path)],
            cwd=ROOT, capture_output=True, text=True, check=False,
            timeout=120,
        )
    finally:
        invalid_path.unlink(missing_ok=True)

    output = result.stdout + result.stderr
    if result.returncode == 0:
        print("FAIL: ESPHome accepted a deliberately missing package include", file=sys.stderr)
        return 1
    if "__ci_missing_olas_gateway__.yaml" not in output:
        print("FAIL: ESPHome failed for an unrelated reason, not the missing include", file=sys.stderr)
        print(output[-2500:], file=sys.stderr)
        return 1
    print("PASS: ESPHome rejected a missing gateway include as expected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
