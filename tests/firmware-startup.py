"""Check that ESPHome's package merge preserves the complete device startup."""

from pathlib import Path
import subprocess
import sys


def check_startup(config):
    lines = config.splitlines()
    start = next(i for i, line in enumerate(lines) if line == "  on_boot:")
    end = next(
        (i for i in range(start + 1, len(lines))
         if lines[i].strip() and len(lines[i]) - len(lines[i].lstrip()) <= 2),
        len(lines),
    )
    startup = "\n".join(lines[start:end])
    required = {
        "theme": "glaze::apply_palette(",
        "weighing interface": "glaze::view.attach(",
        "device identity": "glaze::station.device =",
        "Manager connection": "id: configure_manager",
        "backlight": "id: set_backlight",
        "NFC transmitter": "glaze::configure_nfc_drive(",
        "Wi-Fi scan retention": "set_keep_scan_results(",
    }
    missing = [name for name, marker in required.items() if marker not in startup]
    if missing:
        raise ValueError("Device startup is missing: " + ", ".join(missing))


if __name__ == "__main__":
    fixture = Path(__file__).with_name("firmware.yaml")
    result = subprocess.run(
        [*(sys.argv[1:] or ["esphome"]), "config", str(fixture)],
        capture_output=True, text=True, check=True,
    )
    check_startup(result.stdout)
    print("Verified merged device startup preserves UI, Manager, NFC, and Wi-Fi initialization.")
