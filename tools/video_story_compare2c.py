#!/usr/bin/env python3
"""Build comparison firmware assets: original video frames followed by story layout frames."""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "assets" / "generated"
VIDEO_DIR = OUT_DIR / "video_frames"
COMPARE_DIR = OUT_DIR / "video_story_compare"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--video-threshold", type=int, default=190)
    parser.add_argument("--video-despeckle", type=int, default=1)
    parser.add_argument("--story-threshold", type=int, default=220)
    parser.add_argument("--story-despeckle", type=int, default=0)
    parser.add_argument("--scale", type=int, default=3)
    args = parser.parse_args()

    video2c = load_module("video2c", ROOT / "tools" / "video2c.py")
    story = load_module("story_layout2c", ROOT / "tools" / "story_layout2c.py")

    video_paths = sorted(VIDEO_DIR.glob("video_stage_[0-9][0-9].png"))
    if not video_paths:
        raise FileNotFoundError(f"no video_stage_*.png in {VIDEO_DIR}")

    records = []
    packed = []
    for idx, path in enumerate(video_paths):
        img = Image.open(path).convert("RGB")
        ink = video2c.despeckle(
            video2c.threshold_image(img, args.video_threshold, False),
            args.video_despeckle,
        )
        records.append(("video", idx, img, ink))
        packed.append(video2c.pack_native(ink))
        print(f"video frame {idx:02d} ink {100 * ink.mean():5.1f}% {path.name}")

    stage_frames = story.compose_frames(story.json.loads(story.LAYOUT.read_text(encoding="utf-8")))
    for idx, (stage, img) in enumerate(stage_frames):
        ink = story.to_ink(img, args.story_threshold, 1.0, args.story_despeckle)
        records.append((f"story{stage:02d}", idx, img, ink))
        packed.append(story.pack_native(ink))
        print(f"story frame {idx:02d} stage {stage:2d} ink {100 * ink.mean():5.1f}%")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    COMPARE_DIR.mkdir(parents=True, exist_ok=True)
    story.emit_assets(packed, 200, 200, OUT_DIR)

    thumbs = []
    for global_idx, (label, idx, img, ink) in enumerate(records):
        stem = f"{global_idx:02d}_{label}_{idx:02d}"
        img.save(COMPARE_DIR / f"{stem}_rgb.png")
        bw = Image.fromarray(np.where(ink, 0, 255).astype(np.uint8), "L")
        bw.save(COMPARE_DIR / f"{stem}_1x.png")
        thumbs.append(bw.resize((200 * args.scale, 200 * args.scale), Image.Resampling.NEAREST))

    strip = Image.new("L", (thumbs[0].width * len(thumbs), thumbs[0].height), 255)
    for idx, thumb in enumerate(thumbs):
        strip.paste(thumb, (idx * thumb.width, 0))
    strip.save(OUT_DIR / "video_story_compare_preview.png")

    print(f"\nwrote {len(packed)} frames -> {OUT_DIR / 'family_video_assets.c'}")
    print(f"preview -> {OUT_DIR / 'video_story_compare_preview.png'}")


if __name__ == "__main__":
    main()
