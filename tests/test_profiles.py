"""Profile isolation regression tests using only synthetic data and no hardware."""
import base64
import contextlib
import importlib.util
import io
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("local_profiles", ROOT / "scripts/local.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in ["esphome/examples/local-gateway.yaml.example",
                     "esphome/examples/targets.toml.example", "esphome/secrets.yaml.example",
                     "esphome/olas-rf-gateway.yaml", "esphome/include/olas_protocol.h",
                     "esphome/packages/olas-learning.yaml", "esphome/packages/olas-rf-proxy.yaml"]:
            target = self.root/name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT/name, target)
        self.root_patch = patch.object(runner, "ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        environment = patch.dict(runner.os.environ, {"OLAS_PROFILE": "default", "OLAS_TARGET": "ota"})
        environment.start()
        self.addCleanup(environment.stop)

    def run_action(self, *args):
        with patch.object(sys, "argv", ["local.py", *args]), contextlib.redirect_stdout(io.StringIO()):
            return runner.main()

    def profile(self, name):
        self.run_action("init", "--profile", name)
        return self.root/"esphome/local"/name

    def test_init_preserves_existing_files_and_separates_profiles(self):
        a, b = self.profile("site-a"), self.profile("site-b")
        (a/"secrets.yaml").write_text("private-existing-data", encoding="utf-8")
        before = {p.name: p.read_bytes() for p in a.iterdir()}
        self.run_action("init", "--profile", "site-a")
        self.assertEqual(before, {p.name: p.read_bytes() for p in a.iterdir()})
        self.assertIn("olas-gateway-site-a", (a/"gateway.yaml").read_text())
        self.assertIn("olas-gateway-site-b", (b/"gateway.yaml").read_text())

    def test_target_is_taken_only_from_selected_profile(self):
        a, b = self.profile("site-a"), self.profile("site-b")
        for profile, ip, port in [(a, "192.0.2.10", "COM7"), (b, "192.0.2.20", "/dev/ttyACM0")]:
            (profile/"targets.toml").write_text(f'[targets.ota]\ndevice = "{ip}"\n[targets.usb]\ndevice = "{port}"\n')
        with patch.object(runner.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
            self.run_action("upload", "--profile", "site-a", "--target", "ota")
            self.assertEqual(run.call_args.args[0][-2:], ["--device", "192.0.2.10"])
            self.assertEqual(Path(run.call_args.args[0][2]), a/"gateway.yaml")
            self.run_action("upload", "--profile", "site-b", "--target", "usb")
            self.assertEqual(run.call_args.args[0][-2:], ["--device", "/dev/ttyACM0"])
            self.run_action("compile", "--profile", "site-a")
            self.assertEqual(run.call_args.args[0][1:], ["compile", str(a/"gateway.yaml")])

    def test_invalid_profile_or_unconfigured_target_never_runs_esphome(self):
        self.profile("site-a")
        with patch.object(runner.subprocess, "run") as run, contextlib.redirect_stderr(io.StringIO()):
            for args in [("init", "--profile", "../escape"),
                         ("upload", "--profile", "site-a", "--target", "usb"),
                         ("upload", "--profile", "site-a", "--target", "missing")]:
                with self.assertRaises(SystemExit):
                    self.run_action(*args)
            run.assert_not_called()

    def test_install_builds_selected_profile_and_does_not_upload_on_failure(self):
        profile = self.profile("site-a")
        with patch.object(runner.subprocess, "run") as run:
            run.return_value = subprocess.CompletedProcess([], 1)
            self.assertEqual(self.run_action("install", "--profile", "site-a"), 1)
            run.assert_called_once_with(["esphome", "compile", str(profile/"gateway.yaml")], cwd=self.root)
            run.reset_mock()
            run.return_value = subprocess.CompletedProcess([], 0)
            self.assertEqual(self.run_action("install", "--profile", "site-a"), 0)
            self.assertEqual([call.args[0][1] for call in run.call_args_list], ["compile", "upload"])
            self.assertEqual(run.call_args.args[0][-2:], ["--device", "OTA"])

    def test_esphome_resolves_each_profile_even_with_conflicting_root_secrets(self):
        if not shutil.which("esphome"):
            self.skipTest("Install the pinned ESPHome CLI to run credential-isolation validation")
        wrong = 'wifi_ssid: wrong-root-network\nwifi_password: wrong-root-password\napi_encryption_key: invalid-root-key\nfallback_ap_password: wrong-root-ap\nolas_fan_id: "0xFFFFFF"\nolas_check_xor: "0xFF"\n'
        (self.root/"esphome/secrets.yaml").write_text(wrong)
        for i, name in enumerate(["site-a", "site-b"], start=1):
            profile = self.profile(name)
            key = base64.b64encode(bytes([i])*32).decode()
            (profile/"secrets.yaml").write_text(f'wifi_ssid: test-{name}\nwifi_password: password-{name}\napi_encryption_key: {key}\nfallback_ap_password: fallback-{name}\nolas_fan_id: "0x12345{i}"\nolas_check_xor: "0xA{i}"\n')
            # Every value in this disposable tree is synthetic. Inspect the
            # resolved fields, rather than ESPHome's redacted !secret display.
            result = subprocess.run(["esphome", "config", str(profile/"gateway.yaml"), "--show-secrets"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for value in [f"test-{name}", f"password-{name}", key, f"fallback-{name}", f"0x12345{i}"]:
                self.assertIn(value, result.stdout)
            self.assertNotIn("wrong-root-", result.stdout)
            self.assertNotIn("radio_frequency:", result.stdout)
            self.assertIn("internal: true", result.stdout)
            # Validate merged package entities, not just a stray internal flag:
            # receive-only profiles must hide every new RF action button.
            section = re.search(r"(?ms)^button:\n(.*?)(?=^\S|\Z)", result.stdout).group(1)
            blocks = re.split(r"(?m)^  - ", section)[1:]
            buttons = {match.group(1): block for block in blocks
                       if (match := re.search(r"(?m)^    id: (\S+)$", block))}
            for button_id in ["olas_direction_toggle", "olas_sleep", "olas_timer_1h",
                              "olas_timer_3h", "olas_timer_6h"]:
                self.assertIn("    internal: true\n", buttons[button_id])
            self.assertIn("    has_direction: false\n", result.stdout)
            events = re.search(r"(?ms)^event:\n(.*?)(?=^\S|\Z)", result.stdout).group(1)
            self.assertIn("    id: olas_action\n", events)
            self.assertIn("    internal: true\n", events)
            for action in ["direction", "sleep", "timer_1h", "timer_3h", "timer_6h"]:
                for source in ["requested", "received"]:
                    self.assertIn(f"      - {action}_{source}\n", events)


if __name__ == "__main__":
    unittest.main()
