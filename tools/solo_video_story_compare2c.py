#!/usr/bin/env python3
"""Build firmware assets: solo showcase, original video frames, then story layout frames."""

from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "assets" / "generated"
SOLO_DIR = OUT_DIR / "story_frames"
VIDEO_DIR = OUT_DIR / "video_frames"
COMPARE_DIR = OUT_DIR / "solo_video_story_compare"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def threshold_rgb(img: Image.Image, threshold: int, despeckle_n: int, story_module) -> np.ndarray:
    gray = np.asarray(ImageOps.grayscale(img)).astype(np.float32)
    ink = gray < threshold
    if despeckle_n:
        ink = story_module.despeckle(ink, despeckle_n)
    return ink


def fit_square(img: Image.Image, size: int = 200) -> Image.Image:
    img = img.convert("RGB")
    w, h = img.size
    side = min(w, h)
    left = (w - side) // 2
    top = (h - side) // 2
    return img.crop((left, top, left + side, top + side)).resize((size, size), Image.Resampling.BOX)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--solo-threshold", type=int, default=220)
    parser.add_argument("--solo-despeckle", type=int, default=0)
    parser.add_argument("--video-threshold", type=int, default=190)
    parser.add_argument("--video-despeckle", type=int, default=1)
    parser.add_argument("--story-threshold", type=int, default=220)
    parser.add_argument("--story-despeckle", type=int, default=0)
    parser.add_argument("--scale", type=int, default=3)
    args = parser.parse_args()

    story = load_module("story_layout2c", ROOT / "tools" / "story_layout2c.py")

    records = []
    packed = []

    solo_paths = sorted(SOLO_DIR.glob("solo_stage_*.png"))
    if not solo_paths:
        raise FileNotFoundError(f"no solo_stage_*.png in {SOLO_DIR}")
    for idx, path in enumerate(solo_paths):
        img = fit_square(Image.open(path), 200)
        ink = threshold_rgb(img, args.solo_threshold, args.solo_despeckle, story)
        records.append(("solo", idx, img, ink))
        packed.append(story.pack_native(ink))
        print(f"solo  frame {idx:02d} ink {100 * ink.mean():5.1f}% {path.name}")

    video_paths = sorted(VIDEO_DIR.glob("video_stage_[0-9][0-9].png"))
    if not video_paths:
        raise FileNotFoundError(f"no video_stage_*.png in {VIDEO_DIR}")
    for idx, path in enumerate(video_paths):
        img = Image.open(path).convert("RGB")
        ink = threshold_rgb(img, args.video_threshold, args.video_despeckle, story)
        records.append(("video", idx, img, ink))
        packed.append(story.pack_native(ink))
        print(f"video frame {idx:02d} ink {100 * ink.mean():5.1f}% {path.name}")

    layout = story.json.loads(story.LAYOUT.read_text(encoding="utf-8"))
    for idx, (stage, img) in enumerate(story.compose_frames(layout)):
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
    strip.save(OUT_DIR / "solo_video_story_compare_preview.png")

    print(f"\nwrote {len(packed)} frames -> {OUT_DIR / 'family_video_assets.c'}")
    print(f"preview -> {OUT_DIR / 'solo_video_story_compare_preview.png'}")


if __name__ == "__main__":
    main()
