#!/usr/bin/env python3
"""Build one firmware frame sequence from previous and current story layouts."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "assets" / "generated"
COMPARE_DIR = OUT_DIR / "story_compare"


def load_story_layout2c():
    path = ROOT / "tools" / "story_layout2c.py"
    spec = importlib.util.spec_from_file_location("story_layout2c", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_layout(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--previous", default=str(ROOT / "assets" / "story_layout_previous.json"))
    parser.add_argument("--current", default=str(ROOT / "assets" / "story_layout.json"))
    parser.add_argument("--threshold", type=int, default=220)
    parser.add_argument("--contrast", type=float, default=1.0)
    parser.add_argument("--despeckle", type=int, default=0)
    parser.add_argument("--scale", type=int, default=3)
    args = parser.parse_args()

    story = load_story_layout2c()
    layouts = [
        ("previous", load_layout(Path(args.previous))),
        ("current", load_layout(Path(args.current))),
    ]

    all_stage_frames = []
    all_inks = []
    packed = []
    for label, layout in layouts:
        stage_frames = story.compose_frames(layout)
        for idx, (stage, img) in enumerate(stage_frames):
            ink = story.to_ink(img, args.threshold, args.contrast, args.despeckle)
            all_stage_frames.append((label, idx, stage, img))
            all_inks.append(ink)
            packed.append(story.pack_native(ink))
            print(f"{label:<8} frame {idx:02d} stage {stage:2d} ink {100 * ink.mean():5.1f}%")

    screen = layouts[0][1].get("screen", {"w": 200, "h": 200})
    width = int(screen["w"])
    height = int(screen["h"])
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    COMPARE_DIR.mkdir(parents=True, exist_ok=True)
    story.emit_assets(packed, width, height, OUT_DIR)

    thumbs = []
    for (label, idx, stage, rgb), ink in zip(all_stage_frames, all_inks):
        stem = f"{label}_{idx:02d}_stage_{stage:02d}"
        rgb.save(COMPARE_DIR / f"{stem}_rgb.png")
        bw = Image.fromarray(np.where(ink, 0, 255).astype(np.uint8), "L")
        bw.save(COMPARE_DIR / f"{stem}_1x.png")
        thumbs.append(bw.resize((width * args.scale, height * args.scale), Image.Resampling.NEAREST))

    strip = Image.new("L", (thumbs[0].width * len(thumbs), thumbs[0].height), 255)
    for idx, thumb in enumerate(thumbs):
        strip.paste(thumb, (idx * thumb.width, 0))
    strip.save(OUT_DIR / "story_compare_preview.png")

    print(f"\nwrote {len(packed)} combined frames -> {OUT_DIR / 'family_video_assets.c'}")
    print(f"preview -> {OUT_DIR / 'story_compare_preview.png'}")


if __name__ == "__main__":
    main()
