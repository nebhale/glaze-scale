"""Embed the Glaze Scale Git revision in simulator and firmware builds."""

from pathlib import Path
import subprocess

import esphome.codegen as cg
import esphome.config_validation as cv

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    root = Path(__file__).resolve().parents[2]
    version = "Unversioned"
    if (root / ".git").exists():
        result = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=root,
            capture_output=True,
            text=True,
        )
        if result.returncode == 0:
            version = result.stdout.strip()
    elif (root / "VERSION").is_file():
        version = (root / "VERSION").read_text(encoding="utf-8").strip()
    cg.add_define("GLAZE_SCALE_VERSION", version)
