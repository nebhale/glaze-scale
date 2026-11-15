"""Exercise version detection with the shallow, tag-less checkouts used by ESPHome."""

import importlib.util
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "scale_release_version",
    Path(__file__).resolve().parents[1] / "components/scale_version/version.py",
)
version = importlib.util.module_from_spec(spec)
spec.loader.exec_module(version)


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.directory = TemporaryDirectory(prefix="glaze-version-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.origin = self.root / "origin"
        self.origin.mkdir()
        self.git(self.origin, "init", "--initial-branch=main")
        self.git(self.origin, "config", "user.name", "Version test")
        self.git(self.origin, "config", "user.email", "version@example.invalid")
        self.commit("Released source")

    @staticmethod
    def git(root, *args):
        return subprocess.run(
            ["git", "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", *args],
            cwd=root, capture_output=True, text=True, check=True,
        ).stdout.strip()

    def commit(self, content):
        (self.origin / "source.txt").write_text(content)
        self.git(self.origin, "add", "source.txt")
        self.git(self.origin, "commit", "-m", "Update fixture source")

    def tag(self, name="v1.1.0", annotated=True):
        args = ("-a", name, "-m", name) if annotated else (name,)
        self.git(self.origin, "tag", *args)

    def clone(self):
        cache = self.root / "cache"
        self.git(self.root, "clone", "--depth=1", "--no-tags", self.origin.as_uri(), str(cache))
        self.assertEqual(self.git(cache, "rev-parse", "--is-shallow-repository"), "true")
        return cache

    def test_release_published_after_dashboard_cache_was_created(self):
        cache = self.clone()
        head = self.git(cache, "rev-parse", "HEAD")
        self.assertTrue(head.startswith(self.git(cache, "describe", "--tags", "--always")))
        self.tag()

        self.assertEqual(version.detect_version(cache), "v1.1.0")
        self.assertEqual(self.git(cache, "rev-parse", "HEAD"), head)
        self.assertEqual(self.git(cache, "tag", "--list"), "")

    def test_dashboard_fetches_a_tag_without_creating_a_local_tag_ref(self):
        self.tag()
        cache = self.clone()
        self.git(cache, "fetch", "--depth=1", "--no-tags", "origin", "v1.1.0")
        self.git(cache, "reset", "--hard", "FETCH_HEAD")
        self.assertEqual(self.git(cache, "tag", "--list"), "")
        self.assertEqual(version.detect_version(cache), "v1.1.0")

    def test_lightweight_remote_release_tag(self):
        self.tag(annotated=False)
        self.assertEqual(version.detect_version(self.clone()), "v1.1.0")

    def test_modified_dashboard_checkout_keeps_dirty_suffix(self):
        self.tag()
        cache = self.clone()
        (cache / "source.txt").write_text("Local modification")
        self.assertEqual(version.detect_version(cache), "v1.1.0-dirty")
        self.assertEqual((cache / "source.txt").read_text(), "Local modification")

    def test_local_release_tag_works_without_network_lookup(self):
        self.tag()
        with patch.object(version, "_remote_release", side_effect=AssertionError("Network lookup")):
            self.assertEqual(version.detect_version(self.origin), "v1.1.0")
            (self.origin / "source.txt").write_text("Modified release")
            self.assertEqual(version.detect_version(self.origin), "v1.1.0-dirty")

    def test_newer_remote_release_does_not_label_a_different_commit(self):
        cache = self.clone()
        description = self.git(cache, "describe", "--tags", "--always")
        self.commit("A newer release")
        self.tag()
        self.assertEqual(version.detect_version(cache), description)

    def test_offline_dashboard_build_falls_back_to_commit_hash(self):
        cache = self.clone()
        self.git(cache, "remote", "set-url", "origin", str(self.root / "unavailable"))
        description = self.git(cache, "describe", "--tags", "--always")
        self.assertEqual(version.detect_version(cache), description)

    def test_offline_development_build_keeps_distance_from_local_release(self):
        self.tag()
        self.commit("Development source")
        self.git(self.origin, "remote", "add", "origin", str(self.root / "unavailable"))
        description = self.git(self.origin, "describe", "--tags", "--always")
        self.assertTrue(description.startswith("v1.1.0-1-g"))
        self.assertEqual(version.detect_version(self.origin), description)

    def test_remote_timeout_does_not_fail_the_build(self):
        cache = self.clone()
        description = self.git(cache, "describe", "--tags", "--always")
        run = subprocess.run

        def timeout_remote(args, **kwargs):
            if "ls-remote" in args:
                self.assertEqual(kwargs["timeout"], 10)
                raise subprocess.TimeoutExpired(args, kwargs["timeout"])
            return run(args, **kwargs)

        with patch.object(version.subprocess, "run", side_effect=timeout_remote):
            self.assertEqual(version.detect_version(cache), description)

    def test_archive_version_is_authoritative_even_inside_a_git_checkout(self):
        self.tag("v1.0.0")
        (self.origin / "VERSION").write_text("v1.1.0\n")
        with patch.object(version.subprocess, "run", side_effect=AssertionError("Git lookup")):
            self.assertEqual(version.detect_version(self.origin), "v1.1.0")

    def test_archive_without_git_uses_its_version_stamp(self):
        archive = self.root / "archive"
        archive.mkdir()
        (archive / "VERSION").write_text("v1.1.0\n")
        self.assertEqual(version.detect_version(archive), "v1.1.0")

    def test_source_without_metadata_does_not_use_its_parent_repository(self):
        source = self.origin / "unversioned"
        source.mkdir()
        self.assertEqual(version.detect_version(source), "Unversioned")

    def test_missing_git_does_not_fail_the_build(self):
        with patch.object(version.subprocess, "run", side_effect=FileNotFoundError):
            self.assertEqual(version.detect_version(self.origin), "Unversioned")

    def test_multiple_releases_at_one_commit_prefer_highest_stable_version(self):
        for tag in ("v1.9.0", "v1.10.0-rc.2", "v1.10.0-rc.10", "v1.10.0"):
            self.tag(tag)
        self.assertEqual(version.detect_version(self.clone()), "v1.10.0")

    def test_prerelease_numbers_sort_numerically(self):
        self.tag("v1.1.0-rc.2")
        self.tag("v1.1.0-rc.10")
        self.assertEqual(version.detect_version(self.clone()), "v1.1.0-rc.10")

    def test_build_server_git_environment_cannot_select_another_repository(self):
        self.tag()
        cache = self.clone()
        other = self.root / "other"
        self.git(self.root, "clone", self.origin.as_uri(), str(other))
        self.git(other, "tag", "v9.9.9")
        with patch.dict(os.environ, {
            "GIT_DIR": str(other / ".git"),
            "GIT_WORK_TREE": str(other),
            "GIT_INDEX_FILE": str(other / ".git/index"),
        }):
            self.assertEqual(version.detect_version(cache), "v1.1.0")


if __name__ == "__main__":
    unittest.main()
