#!/usr/bin/env python3
"""Validate a story package before it is copied to an SD card.

The checker deliberately validates both the source scene PNGs used by the
FVID builder and the scene/audio references in story.json. It is intended to
be cheap enough to run after every asset replacement.
"""

from __future__ import annotations

import argparse
import json
import struct
import wave
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps


def check_story(story_dir: Path) -> int:
    build_path = story_dir / "build.json"
    story_root = story_dir / story_dir.name
    story_path = story_root / "story.json"
    if not build_path.is_file() or not story_path.is_file():
        raise FileNotFoundError(f"expected build.json and {story_dir.name}/story.json in {story_dir}")

    build = json.loads(build_path.read_text(encoding="utf-8"))
    build_spec = build.get("stories", [build])[0]
    story = json.loads(story_path.read_text(encoding="utf-8"))
    scenes = story.get("scenes", [])
    audio_pending = story.get("audio_status") == "pending"
    errors = 0
    warnings = 0

    def error(message: str) -> None:
        nonlocal errors
        errors += 1
        print(f"FAIL {message}")

    def warning(message: str) -> None:
        nonlocal warnings
        warnings += 1
        print(f"WARN {message}")

    if story.get("version") != 1:
        error("story.json version must be 1")
    for key in ("id", "title", "author", "language", "age_rating", "voice", "audio_format"):
        if not story.get(key):
            error(f"story.json missing {key}")
    if [scene.get("index") for scene in scenes] != list(range(len(scenes))):
        error("scene indexes are not continuous from zero")
    if not scenes:
        error("story has no scenes")

    script = (story_root / "ai_studio_script.md").read_text(encoding="utf-8") \
        if (story_root / "ai_studio_script.md").is_file() else ""
    source_dir = story_dir / build_spec.get("frames_dir", "source/scenes")
    glob = build_spec.get("glob", "*.png")
    frame_paths = sorted(source_dir.glob(glob))
    if len(frame_paths) != len(scenes):
        error(f"scene count {len(scenes)} does not match frame count {len(frame_paths)}")

    seen_scene_images: set[Path] = set()
    for scene in scenes:
        index = scene.get("index")
        narration = scene.get("narration", "")
        image_ref = scene.get("image")
        audio_ref = scene.get("audio")
        if not narration or narration not in script:
            error(f"scene {index}: narration is missing or differs from ai_studio_script.md")
        if not image_ref or not audio_ref:
            error(f"scene {index}: image/audio reference missing")
            continue
        image_path = (story_path.parent / image_ref).resolve()
        audio_path = (story_path.parent / audio_ref).resolve()
        seen_scene_images.add(image_path)
        if not image_path.is_file():
            error(f"scene {index}: missing image {image_ref}")
        else:
            with Image.open(image_path) as image:
                if image.size != (200, 200):
                    error(f"scene {index}: image size is {image.size}, expected (200, 200)")
                gray = ImageOps.grayscale(image)
                ink = np.asarray(gray) < int(build_spec.get("threshold", 190))
                ratio = float(np.count_nonzero(ink)) / ink.size
                status = "PASS"
                if ratio < 0.05 or ratio > 0.38:
                    status = "WARN"
                    warning(f"scene {index}: ink_ratio={ratio:.3f} outside target 0.08..0.32")
                if ratio > 0.45:
                    error(f"scene {index}: ink_ratio={ratio:.3f} exceeds hard limit 0.45")
                print(f"{status} scene={index} image={image_path.name} size=200x200 ink_ratio={ratio:.3f}")
        if not audio_path.is_file():
            message = f"scene {index}: missing audio {audio_ref}"
            if audio_pending:
                warning(f"{message}; audio_status=pending")
            else:
                error(message)
        else:
            try:
                with wave.open(str(audio_path), "rb") as audio:
                    params = audio.getparams()
                    valid = (params.nchannels, params.sampwidth, params.framerate) == (1, 2, 16000)
                    if not valid:
                        error(f"scene {index}: WAV format is {params}, expected mono/16-bit/16kHz")
                    if params.nframes == 0:
                        error(f"scene {index}: WAV is empty")
                    duration = params.nframes / params.framerate
                    if duration > 25:
                        warning(f"scene {index}: duration={duration:.2f}s exceeds recommended 25s; split or regenerate")
                    print(f"{'PASS' if valid else 'FAIL'} scene={index} audio={audio_path.name} duration={duration:.2f}s")
            except wave.Error as exc:
                error(f"scene {index}: invalid WAV: {exc}")

    if len(seen_scene_images) != len(scenes):
        error("scene image references are not unique")
    fvid = story_dir / build_spec.get("output", f"{story.get('id', story_dir.name)}.fvid")
    if not fvid.is_file():
        warning(f"missing built FVID {fvid.name}; run tools/build_stories.py first")
    else:
        raw = fvid.read_bytes()
        if len(raw) < 16 or raw[:4] != b"FVID":
            error(f"invalid FVID header: {fvid.name}")
        else:
            _, version, _, width, height, count, _ = struct.unpack_from("<4sBBHHH4s", raw, 0)
            if (version, width, height) != (1, 200, 200):
                error(f"FVID geometry/version is {version}/{width}x{height}, expected 1/200x200")
            expected = 16 + count * 5004
            if len(raw) != expected or count != len(scenes):
                error(f"FVID contains {count} frames and {len(raw)} bytes; expected {len(scenes)} frames/{expected} bytes")
            for index in range(min(count, len(scenes))):
                frame = raw[16 + index * 5004 + 4:16 + (index + 1) * 5004]
                ratio = sum(8 - byte.bit_count() for byte in frame) / (200 * 200)
                if ratio < 0.05 or ratio > 0.38:
                    warning(f"FVID frame {index}: ink_ratio={ratio:.3f} outside target 0.08..0.32")
                if ratio > 0.45:
                    error(f"FVID frame {index}: ink_ratio={ratio:.3f} exceeds hard limit 0.45")
                print(f"{'PASS' if 0.05 <= ratio <= 0.38 else 'WARN'} fvid_frame={index} ink_ratio={ratio:.3f}")
    print(f"SUMMARY story={story.get('id', story_dir.name)} scenes={len(scenes)} warnings={warnings} errors={errors}")
    return 1 if errors else 0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("story_dir", type=Path)
    args = parser.parse_args()
    raise SystemExit(check_story(args.story_dir.resolve()))


if __name__ == "__main__":
    main()
