#!/usr/bin/env python3
"""Check every story package and summarize formal-package readiness."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("story_root", type=Path, nargs="?", default=Path("assets/stories"))
    args = parser.parse_args()

    root = args.story_root.resolve()
    checker = Path(__file__).with_name("check_story_assets.py")
    packages = sorted(
        path for path in root.iterdir()
        if path.is_dir() and ((path / "story.json").is_file() or
                              (path / path.name / "story.json").is_file())
    )
    if not packages:
        print(f"FAIL no story packages found in {root}")
        return 1

    failures = 0
    ready: list[str] = []
    pending: list[str] = []
    for package in packages:
        result = subprocess.run(
            [sys.executable, str(checker), str(package)],
            check=False,
        )
        if result.returncode != 0:
            failures += 1
            continue
        metadata_path = package / "story.json"
        if not metadata_path.is_file():
            metadata_path = package / package.name / "story.json"
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        story_id = metadata.get("id", package.name)
        if metadata.get("audio_status") in ("pending", "missing"):
            pending.append(story_id)
        else:
            ready.append(story_id)

    print("LIBRARY_SUMMARY packages=%d ready=%d pending_audio=%d failures=%d" %
          (len(packages), len(ready), len(pending), failures))
    if ready:
        print("READY " + ", ".join(ready))
    if pending:
        print("PENDING_AUDIO " + ", ".join(pending))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
