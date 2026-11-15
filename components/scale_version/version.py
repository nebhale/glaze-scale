"""Resolve release labels even when ESPHome's shallow cache has no tag refs."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess


def _git(root: Path, *args: str) -> str | None:
    # Build-server Git settings must not redirect queries to another repository.
    scoped = {
        "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR",
        "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_NAMESPACE",
    }
    env = {key: value for key, value in os.environ.items() if key not in scoped}
    env["GIT_TERMINAL_PROMPT"] = "0"
    try:
        result = subprocess.run(
            ["git", *args], cwd=root, capture_output=True, text=True, timeout=10, env=env,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def _release_key(tag: str):
    match = re.fullmatch(
        r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
        r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?", tag,
    )
    if match is None:
        return None
    major, minor, patch, suffix = match.groups()
    prerelease = tuple(
        (0, int(part)) if part.isascii() and part.isdigit() else (1, part)
        for part in suffix.split(".")
    ) if suffix else ()
    return (int(major), int(minor), int(patch), suffix is None, prerelease)


def _remote_release(root: Path, head: str) -> str | None:
    # Read refs without fetching/resetting ESPHome's shared checkout. Annotated
    # tags advertise their commit separately with the ^{} suffix.
    refs = _git(root, "ls-remote", "--tags", "origin", "refs/tags/v*")
    candidates = {}
    for line in (refs or "").splitlines():
        parts = line.split("\t", 1)
        if len(parts) != 2:
            continue
        sha, ref = parts
        if sha != head or not ref.startswith("refs/tags/"):
            continue
        tag = ref.removeprefix("refs/tags/").removesuffix("^{}")
        if (key := _release_key(tag)) is not None:
            candidates[tag] = key
    return max(candidates, key=candidates.get) if candidates else None


def detect_version(root: Path) -> str:
    stamp = root / "VERSION"
    if stamp.is_file() and (version := stamp.read_text(encoding="utf-8").strip()):
        return version
    if not (root / ".git").exists():
        return "Unversioned"

    description = _git(root, "describe", "--tags", "--always", "--dirty")
    if _git(root, "describe", "--tags", "--exact-match"):
        return description or "Unversioned"
    if head := _git(root, "rev-parse", "HEAD"):
        if tag := _remote_release(root, head):
            return tag + ("-dirty" if description and description.endswith("-dirty") else "")
    return description or "Unversioned"
