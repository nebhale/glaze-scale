"""Embed the Glaze Scale Git revision in simulator and firmware builds."""

from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv

from .version import detect_version

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    root = Path(__file__).resolve().parents[2]
    cg.add_define("GLAZE_SCALE_VERSION", detect_version(root))
