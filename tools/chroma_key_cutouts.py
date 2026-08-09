#!/usr/bin/env python3
"""Create transparent PNG cutouts from green-screen line-art source images."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "assets" / "src"
OUT_DIR = ROOT / "assets" / "generated" / "cutouts"


def is_image(path: Path) -> bool:
    return path.suffix.lower() in {".png", ".jpg", ".jpeg", ".webp"}


def is_candidate(path: Path) -> bool:
    name = path.stem.lower()
    if name in {"background", "bg"}:
        return False
    return is_image(path)


def majority3(mask: np.ndarray) -> np.ndarray:
    padded = np.pad(mask.astype(np.uint8), 1, mode="edge")
    acc = np.zeros(mask.shape, dtype=np.uint8)
    for dy in range(3):
        for dx in range(3):
            acc += padded[dy:dy + mask.shape[0], dx:dx + mask.shape[1]]
    return acc >= 5


def bbox(mask: np.ndarray, padding: int) -> tuple[int, int, int, int]:
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        raise ValueError("no foreground after keying")
    h, w = mask.shape
    return (
        max(0, int(xs.min()) - padding),
        max(0, int(ys.min()) - padding),
        min(w, int(xs.max()) + 1 + padding),
        min(h, int(ys.max()) + 1 + padding),
    )


def key_green(img: Image.Image, threshold: int, soften: int, despill: bool) -> Image.Image:
    rgb = np.asarray(img.convert("RGB")).astype(np.uint8)
    r = rgb[..., 0].astype(np.int16)
    g = rgb[..., 1].astype(np.int16)
    b = rgb[..., 2].astype(np.int16)

    green_score = g - np.maximum(r, b)
    hard_fg = green_score <= threshold
    hard_fg = majority3(hard_fg)

    if soften > 0:
        alpha = np.clip((threshold + soften - green_score) / max(1, soften), 0, 1)
        alpha = np.where(hard_fg, alpha, 0.0)
    else:
        alpha = hard_fg.astype(np.float32)

    out = rgb.astype(np.float32)
    if despill:
        cap = np.maximum(r, b).astype(np.float32)
        spill = hard_fg & (g > cap)
        out[..., 1] = np.where(spill, cap, out[..., 1])

    rgba = np.dstack([np.clip(out, 0, 255).astype(np.uint8),
                      np.round(alpha * 255).astype(np.uint8)])
    return Image.fromarray(rgba, "RGBA")


def process(path: Path, out_dir: Path, args: argparse.Namespace) -> Path:
    cutout = key_green(Image.open(path), args.threshold, args.soften, not args.no_despill)
    alpha = np.asarray(cutout)[..., 3] > args.alpha_crop
    x0, y0, x1, y1 = bbox(alpha, args.padding)
    cutout = cutout.crop((x0, y0, x1, y1))

    out = out_dir / f"{path.stem}_cutout.png"
    cutout.save(out)
    area = 100 * alpha.mean()
    print(f"{path.name:<58} foreground {area:5.1f}% -> {out.name}")
    return out


def make_preview(paths: list[Path], out_dir: Path) -> None:
    if not paths:
        return
    thumbs = []
    for path in paths:
        img = Image.open(path).convert("RGBA")
        img.thumbnail((160, 160), Image.Resampling.LANCZOS)
        tile = Image.new("RGBA", (180, 180), (255, 255, 255, 255))
        checker = Image.new("RGBA", tile.size, (255, 255, 255, 255))
        pix = checker.load()
        for y in range(0, checker.height, 12):
            for x in range(0, checker.width, 12):
                c = (232, 232, 232, 255) if ((x + y) // 12) % 2 else (255, 255, 255, 255)
                for yy in range(y, min(y + 12, checker.height)):
                    for xx in range(x, min(x + 12, checker.width)):
                        pix[xx, yy] = c
        tile = Image.alpha_composite(checker, tile)
        tile.alpha_composite(img, ((180 - img.width) // 2, (180 - img.height) // 2))
        thumbs.append(tile.convert("RGB"))

    cols = min(4, len(thumbs))
    rows = (len(thumbs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * 180, rows * 180), "white")
    for idx, thumb in enumerate(thumbs):
        sheet.paste(thumb, ((idx % cols) * 180, (idx // cols) * 180))
    sheet.save(out_dir / "cutouts_preview.png")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--src", default=str(SRC_DIR))
    parser.add_argument("--out", default=str(OUT_DIR))
    parser.add_argument("--threshold", type=int, default=40)
    parser.add_argument("--soften", type=int, default=12)
    parser.add_argument("--padding", type=int, default=16)
    parser.add_argument("--alpha-crop", type=int, default=12)
    parser.add_argument("--no-despill", action="store_true")
    args = parser.parse_args()

    src_dir = Path(args.src)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    outputs = []
    for path in sorted(src_dir.iterdir()):
        if is_candidate(path):
            outputs.append(process(path, out_dir, args))
    make_preview(outputs, out_dir)
    print(f"\nwrote {len(outputs)} cutouts -> {out_dir}")


if __name__ == "__main__":
    main()
