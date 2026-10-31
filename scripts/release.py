#!/usr/bin/env python3
"""Prepare and optionally publish a firmware release."""

from __future__ import annotations

import argparse
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path

from generate_version import (
    ROOT,
    SEMVER_RE,
    VersionMetadata,
    make_dev_version,
    prepare_changelog,
    read_release_version,
    validate_changelog,
    write_header,
)


RELEASE_METADATA_PATHS = {
    "VERSION",
    "CHANGELOG.md",
    "include/version.h",
}


def run_git(args: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=ROOT,
        check=check,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def write_version(version_file: Path, version: str) -> None:
    if not SEMVER_RE.fullmatch(version):
        raise SystemExit("Version must be SemVer without build metadata, for example 1.2.3 or 1.2.3-rc.1")

    version_file.write_text(f"{version}\n", encoding="utf-8")


def changelog_has_release(changelog: Path, release_version: str) -> bool:
    heading = re.escape(f"## [{release_version}]")
    text = changelog.read_text(encoding="utf-8")
    return re.search(rf"^{heading}(?: - .+)?$", text, re.MULTILINE) is not None


def write_local_header(
    header: Path,
    release_version: str,
    major: int,
    minor: int,
    patch: int,
) -> None:
    metadata = VersionMetadata(
        version=make_dev_version(release_version, "local"),
        release_version=release_version,
        base_version=f"{major}.{minor}.{patch}",
        major=major,
        minor=minor,
        patch=patch,
        git_sha="local",
        build_timestamp="local",
        build_date="local",
        changelog_section="Unreleased",
        is_release=False,
    )
    write_header(header, metadata)


def list_dirty_paths() -> list[str]:
    result = run_git(["status", "--porcelain=v1", "--untracked-files=all"])
    dirty_paths: list[str] = []

    for raw_line in result.stdout.splitlines():
        if not raw_line:
            continue

        path = raw_line[3:]
        if " -> " in path:
            path = path.split(" -> ", 1)[1]

        dirty_paths.append(path.replace("\\", "/"))

    return dirty_paths


def assert_no_unrelated_dirty_paths() -> None:
    unrelated = [
        path for path in list_dirty_paths()
        if path not in RELEASE_METADATA_PATHS
    ]
    if unrelated:
        formatted = "\n".join(f"  - {path}" for path in unrelated)
        raise SystemExit(
            "Refusing to commit/tag release while unrelated files are dirty:\n"
            f"{formatted}\n"
            "Commit or stash those changes first."
        )


def assert_clean_worktree(action: str) -> None:
    dirty = list_dirty_paths()
    if dirty:
        formatted = "\n".join(f"  - {path}" for path in dirty)
        raise SystemExit(
            f"Refusing to {action} while the working tree has uncommitted changes:\n"
            f"{formatted}\n"
            "Commit or stash those changes first."
        )


def commit_release_metadata(release_version: str) -> None:
    assert_no_unrelated_dirty_paths()
    run_git(["add", *sorted(RELEASE_METADATA_PATHS)])

    diff = run_git(["diff", "--cached", "--quiet"], check=False)
    if diff.returncode == 0:
        print("No release metadata changes to commit.")
        return

    run_git(["commit", "-m", f"chore: prepare release v{release_version}"])


def tag_exists(release_version: str) -> bool:
    tag = f"v{release_version}"
    existing = run_git(["rev-parse", "-q", "--verify", f"refs/tags/{tag}"], check=False)
    return existing.returncode == 0


def tag_release(release_version: str) -> None:
    assert_clean_worktree("tag the release")
    tag = f"v{release_version}"
    if tag_exists(release_version):
        raise SystemExit(f"Tag {tag} already exists.")

    run_git(["tag", "-a", tag, "-m", f"Release {tag}"])


def push_release(remote: str, release_version: str) -> None:
    assert_clean_worktree("push the release")
    tag = f"v{release_version}"
    branch = run_git(["rev-parse", "--abbrev-ref", "HEAD"]).stdout.strip()
    if branch == "HEAD":
        raise SystemExit("Refusing to push from detached HEAD.")

    run_git(["push", remote, branch])
    run_git(["push", remote, tag])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", help="Set VERSION before preparing the release.")
    parser.add_argument("--version-file", type=Path, default=ROOT / "VERSION")
    parser.add_argument("--changelog", type=Path, default=ROOT / "CHANGELOG.md")
    parser.add_argument("--header", type=Path, default=ROOT / "include" / "version.h")
    parser.add_argument(
        "--release-date",
        default=datetime.now(timezone.utc).strftime("%Y-%m-%d"),
        help="Release date for CHANGELOG.md, in YYYY-MM-DD format.",
    )
    parser.add_argument("--no-header", action="store_true", help="Do not update include/version.h fallback.")
    parser.add_argument("--commit", action="store_true", help="Commit release metadata changes.")
    parser.add_argument("--tag", action="store_true", help="Create an annotated release tag.")
    parser.add_argument("--push", action="store_true", help="Push the current branch and release tag.")
    parser.add_argument("--remote", default="origin", help="Git remote used by --push.")
    args = parser.parse_args()

    if args.version:
        write_version(args.version_file, args.version)

    release_version, major, minor, patch = read_release_version(args.version_file)

    if changelog_has_release(args.changelog, release_version):
        print(f"CHANGELOG.md already contains release section {release_version}.")
    else:
        prepare_changelog(args.changelog, release_version, args.release_date)
        print(f"Prepared CHANGELOG.md section {release_version}.")

    validate_changelog(args.changelog, release_version)

    if not args.no_header:
        write_local_header(args.header, release_version, major, minor, patch)
        print("Updated include/version.h local fallback.")

    if args.commit:
        commit_release_metadata(release_version)
        print(f"Committed release metadata for v{release_version}.")

    if args.tag:
        tag_release(release_version)
        print(f"Created tag v{release_version}.")

    if args.push:
        if not args.tag and not tag_exists(release_version):
            raise SystemExit("--push requires --tag in the same command or an existing release tag.")
        push_release(args.remote, release_version)
        print(f"Pushed release v{release_version} to {args.remote}.")

    if not (args.commit or args.tag or args.push):
        print("Review the diff, then run with --commit --tag --push when ready.")


if __name__ == "__main__":
    main()
