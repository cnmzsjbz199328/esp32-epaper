"""Shared FVID v1 writer and refresh-hint policy for asset generators."""

from __future__ import annotations

import struct
from pathlib import Path

import numpy as np

HINT_PARTIAL = "APP_REFRESH_PARTIAL"
HINT_FULL = "APP_REFRESH_FULL"
HINT_FINAL_FULL = "APP_REFRESH_FINAL_FULL"


def refresh_hints(inks: list[np.ndarray]) -> list[str]:
    """Choose the same conservative hints used by video2c.py."""
    hints: list[str] = []
    previous = None
    total = len(inks)
    for index, ink in enumerate(inks):
        changed_pct = 100.0 if previous is None else (
            100.0 * np.count_nonzero(previous != ink) / ink.size
        )
        if index == 0:
            hint = HINT_FULL
        elif index == total - 1:
            hint = HINT_FINAL_FULL
        elif changed_pct > 35.0:
            hint = HINT_FULL
        else:
            hint = HINT_PARTIAL
        hints.append(hint)
        previous = ink
    return hints


def emit_fvid(frames, hints: list[str], width: int, height: int, output: Path) -> Path:
    """Write a panel-native FVID v1 container."""
    frame_len = width * height // 8
    if frame_len != 5000:
        raise ValueError("FVID currently requires the panel-native 200x200 framebuffer")
    if len(frames) != len(hints):
        raise ValueError("frames and hints must have the same length")

    hint_values = {HINT_PARTIAL: 0, HINT_FULL: 1, HINT_FINAL_FULL: 2}
    payload = bytearray(struct.pack(
        "<4sBBHHH4s", b"FVID", 1, 0, width, height, len(frames), b"\0\0\0\0"
    ))
    for frame, hint in zip(frames, hints):
        if hint not in hint_values:
            raise ValueError(f"unknown refresh hint: {hint}")
        raw = bytes(int(value) for value in frame)
        if len(raw) != frame_len:
            raise ValueError(f"frame length {len(raw)} != {frame_len}")
        payload.extend(bytes((hint_values[hint], 0, 0, 0)))
        payload.extend(raw)

    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)
    return output
