"""Select a private profile and an independent OTA/serial target."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tomllib

ROOT = Path(__file__).resolve().parent.parent


def identifier(value):
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,31}", value):
        raise argparse.ArgumentTypeError("Use 1-32 lowercase letters, digits, _ or -.")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["init", "config", "compile", "install", "upload", "logs"])
    parser.add_argument("--profile", type=identifier,
                        default=os.environ.get("OLAS_PROFILE", "default"))
    parser.add_argument("--target", type=identifier,
                        default=os.environ.get("OLAS_TARGET", "ota"))
    args = parser.parse_args()
    local_root = ROOT / "esphome/local"
    profile = local_root / args.profile
    # Reject symlinks/junctions that could leave the private workspace.
    if not profile.resolve().is_relative_to(local_root.resolve()):
        parser.error("Profile must stay inside esphome/local.")
    config = profile / "gateway.yaml"
    if args.command == "init":
        profile.mkdir(parents=True, exist_ok=True)
        for source, name in [
            ("esphome/examples/local-gateway.yaml.example", "gateway.yaml"),
            ("esphome/secrets.yaml.example", "secrets.yaml"),
            ("esphome/examples/targets.toml.example", "targets.toml"),
        ]:
            target = profile / name
            # Exclusive creation preserves existing files, including symlinks.
            try:
                with target.open("x", encoding="utf-8") as output:
                    output.write((ROOT / source).read_text(encoding="utf-8")
                                 .replace("__PROFILE__", args.profile.replace("_", "-")))
                print(f"Created {target.relative_to(ROOT)}")
            except FileExistsError:
                print(f"Kept existing {target.relative_to(ROOT)}")
        print("Fill private secrets/targets. New profiles start with RF control disabled.")
        return 0
    if not config.is_file():
        parser.error("Profile not found; run init-local with --profile first.")
    command = ["esphome", "upload" if args.command == "install" else args.command, str(config)]
    if args.command in ("install", "upload", "logs"):
        try:
            targets = tomllib.loads((profile / "targets.toml").read_text(encoding="utf-8"))
            device = targets["targets"][args.target]["device"]
        except (OSError, KeyError, tomllib.TOMLDecodeError) as error:
            parser.error(f"Cannot select target {args.target}: {error}")
        if not isinstance(device, str) or not device.strip() or device == "CHANGE_ME" or device.startswith("-"):
            parser.error("Set this profile's target device to an IP/host, OTA or serial port.")
        command += ["--device", device]
    print(f"Profile: {args.profile}; action: {args.command}", flush=True)
    if args.command == "install":
        result = subprocess.run(["esphome", "compile", str(config)], cwd=ROOT)
        if result.returncode:
            return result.returncode
    return subprocess.run(command, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
