"""Test the Olas component schema through the real ESPHome config pipeline."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = (
    (ROOT / "tests/fixtures/olas-component.yaml")
    .read_text()
    .replace("../../components", str(ROOT / "components"))
)


class ComponentConfigTests(unittest.TestCase):
    def validate(self, content, expected_error=None):
        with tempfile.TemporaryDirectory(prefix="olas-config-") as tmp:
            path = Path(tmp) / "test.yaml"
            path.write_text(content)
            result = subprocess.run(
                ["esphome", "config", str(path)],
                capture_output=True,
                text=True,
                timeout=120,
            )
        output = result.stdout + result.stderr  # The fixture has no credentials.
        if expected_error is None:
            self.assertEqual(result.returncode, 0, output)
        else:
            self.assertNotEqual(result.returncode, 0, output)
            self.assertIn(expected_error, output)

    def test_two_devices(self):
        self.validate(FIXTURE)

    def test_boundaries(self):
        self.validate(
            FIXTURE.replace("0x123456", "0")
            .replace("0x654321", "0xFFFFFF")
            .replace("0xA7", "0")
            .replace("0x39", "255")
        )

    def test_duplicate_addresses(self):
        self.validate(
            FIXTURE.replace("0x654321", "0x123456"), "Duplicate Olas address"
        )

    def test_duplicate_ids(self):
        self.validate(FIXTURE.replace("id: olas_b", "id: olas_a"), "redefined")

    def test_bad_addresses(self):
        for value in ["-1", "0x1000000", "1.5", "1.0", "not_an_address", "true"]:
            with self.subTest(value=value):
                self.validate(FIXTURE.replace("0x123456", value), "address")

    def test_bad_checks(self):
        for value in ["-1", "256", "1.5", "1.0", "not_a_check", "true"]:
            with self.subTest(value=value):
                self.validate(FIXTURE.replace("0xA7", value), "check_xor")

    def test_required_fields(self):
        for line, field in [
            ("      address: 0x123456\n", "address"),
            ("      check_xor: 0xA7\n", "check_xor"),
            ("      name: Fan A\n", "name"),
            ("  receiver_id: olas_rx\n", "receiver_id"),
            ("  transmitter_id: olas_tx\n", "transmitter_id"),
        ]:
            with self.subTest(field=field):
                self.validate(FIXTURE.replace(line, ""), field)
        self.validate(FIXTURE[: FIXTURE.index("  devices:")], "devices")

    def test_learning_only_without_devices(self):
        head = FIXTURE[: FIXTURE.index("  devices:")]
        self.validate(head + "  devices: []\n")

    def test_entity_names_can_be_overridden(self):
        self.validate(
            FIXTURE.replace(
                "      name: Fan A\n",
                "      name: Fan A\n      light:\n        name: Lamp A\n",
            )
        )

    def test_duplicate_device_names_are_rejected(self):
        self.validate(FIXTURE.replace("name: Fan B", "name: Fan A"), "Duplicate")

    def test_invalid_transport_references(self):
        for field, original in [
            ("receiver_id", "olas_rx"),
            ("transmitter_id", "olas_tx"),
        ]:
            with self.subTest(field=field, kind="missing"):
                self.validate(
                    FIXTURE.replace(f"{field}: {original}", f"{field}: missing_one"),
                    "missing_one",
                )
            with self.subTest(field=field, kind="wrong_type"):
                self.validate(
                    FIXTURE.replace(f"{field}: {original}", f"{field}: olas_a"),
                    "olas_a",
                )


if __name__ == "__main__":
    unittest.main()
