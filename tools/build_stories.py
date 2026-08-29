#!/usr/bin/env python3
"""Build the SD story library from a small JSON manifest."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps

from _fvid import emit_fvid, refresh_hints

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = ROOT / "assets" / "story_library.json"
DEFAULT_OUT = ROOT / "assets" / "generated" / "stories"


def despeckle(ink: np.ndarray, min_neighbors: int) -> np.ndarray:
    if min_neighbors <= 0:
        return ink
    p = np.pad(ink.astype(np.uint8), 1)
    acc = np.zeros(ink.shape, dtype=np.uint8)
    for dy in range(3):
        for dx in range(3):
            if dy != 1 or dx != 1:
                acc += p[dy:dy + ink.shape[0], dx:dx + ink.shape[1]]
    return ink & (acc >= min_neighbors)


def pack_native(ink: np.ndarray) -> np.ndarray:
    height, width = ink.shape
    stride = (width + 7) // 8
    output = np.full((height, stride), 0xFF, dtype=np.uint8)
    for x in range(width):
        bit = np.uint8(0x80 >> (x & 7))
        output[:, x >> 3] &= np.where(ink[:, x], np.uint8(~bit & 0xFF), np.uint8(0xFF))
    return output.reshape(-1)


def build_story(spec: dict, manifest_path: Path, out_dir: Path) -> Path:
    source_dir = (manifest_path.parent / spec["frames_dir"]).resolve()
    paths = sorted(source_dir.glob(spec.get("glob", "*.png")))
    if not paths:
        raise FileNotFoundError(f"no frames matched {source_dir / spec.get('glob', '*.png')}")

    size = int(spec.get("size", 200))
    threshold = int(spec.get("threshold", 190))
    despeckle_n = int(spec.get("despeckle", 0))
    frames = []
    inks = []
    for path in paths:
        image = Image.open(path).convert("RGB").resize((size, size))
        ink = np.asarray(ImageOps.grayscale(image)) < threshold
        ink = despeckle(ink, despeckle_n)
        frames.append(pack_native(ink))
        inks.append(ink)

    output = out_dir / spec["output"]
    emit_fvid(frames, refresh_hints(inks), size, size, output)
    print(f"{spec['name']}: {len(frames)} frames -> {output}")
    return output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", default=str(DEFAULT_MANIFEST))
    parser.add_argument("--out-dir", default=str(DEFAULT_OUT))
    args = parser.parse_args()

    manifest_path = Path(args.manifest)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for spec in manifest.get("stories", []):
        build_story(spec, manifest_path, out_dir)


if __name__ == "__main__":
    main()
