#!/usr/bin/env python3
"""Convert the legacy family_video_assets.c framebuffer arrays to FVID v1.

The parser intentionally consumes the generated C ABI instead of rendering the
source images again, so byte order, frame order, and refresh hints are retained
exactly.  The command fails if the generated file is incomplete or malformed.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
from pathlib import Path


FRAME_RE = re.compile(
    r"const\s+uint8_t\s+APP_VIDEO_FRAME_(\d+)\[5000\]\s*=\s*\{(.*?)\};",
    re.DOTALL,
)
HINT_RE = re.compile(r"APP_VIDEO_REFRESH_HINTS\[\]\s*=\s*\{(.*?)\};", re.DOTALL)


def convert(source: Path, destination: Path) -> tuple[int, str]:
    text = source.read_text(encoding="utf-8")
    frames: dict[int, bytes] = {}
    for match in FRAME_RE.finditer(text):
        index = int(match.group(1))
        values = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", match.group(2)))
        if len(values) != 5000:
            raise ValueError(f"frame {index} contains {len(values)} bytes, expected 5000")
        frames[index] = values
    if not frames or sorted(frames) != list(range(len(frames))):
        raise ValueError("frame arrays are missing or not continuous from zero")

    hint_match = HINT_RE.search(text)
    if not hint_match:
        raise ValueError("APP_VIDEO_REFRESH_HINTS is missing")
    names = re.findall(r"APP_REFRESH_(PARTIAL|FULL|FINAL_FULL)", hint_match.group(1))
    hint_values = {"PARTIAL": 0, "FULL": 1, "FINAL_FULL": 2}
    if len(names) != len(frames):
        raise ValueError(f"found {len(names)} refresh hints for {len(frames)} frames")

    payload = bytearray(struct.pack("<4sBBHHH4s", b"FVID", 1, 0, 200, 200, len(frames), b"\0" * 4))
    for index, name in enumerate(names):
        payload.extend(bytes((hint_values[name], 0, 0, 0)))
        payload.extend(frames[index])
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(payload)
    return len(frames), hashlib.sha256(payload).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--expected-frames", type=int)
    parser.add_argument("--expected-sha256")
    args = parser.parse_args()
    count, digest = convert(args.source, args.destination)
    if args.expected_frames is not None and count != args.expected_frames:
        raise SystemExit(f"FAIL frame count {count} != expected {args.expected_frames}")
    if args.expected_sha256 and digest.lower() != args.expected_sha256.lower():
        raise SystemExit(f"FAIL sha256 {digest} != expected {args.expected_sha256}")
    print(f"PASS frames={count} sha256={digest} output={args.destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
