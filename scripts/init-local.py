"""Create a private hardware-test profile without overwriting existing files."""
from pathlib import Path
import shutil

root = Path(__file__).resolve().parent.parent
local = root / "esphome" / "local"
local.mkdir(exist_ok=True)
for source, name in (
    (root / "esphome/examples/local-gateway.yaml.example", "gateway.yaml"),
    (root / "esphome/secrets.yaml.example", "secrets.yaml"),
):
    target = local / name
    if target.exists():
        print(f"Kept existing esphome/local/{name}")
    else:
        shutil.copyfile(source, target)
        print(f"Created esphome/local/{name}")
print("Edit private secrets before validation. New template profiles start with RF control disabled.")
