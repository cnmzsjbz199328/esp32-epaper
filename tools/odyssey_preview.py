#!/usr/bin/env python3
"""Small desktop previewer for the Odyssey story package.

The tool intentionally targets one app and one package.  It shows the actual
decoded FVID frame beside the scene metadata and plays the scene WAV.  The
sequence log makes the important ordering visible:

    display request -> refresh complete -> audio start

On Windows, audio uses the built-in ``winsound`` module.  Pillow is used for
the 200x200 image and card rendering; it is already a dependency of the story
asset checker.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

try:
    import winsound
except ImportError:  # pragma: no cover - only used on non-Windows machines
    winsound = None

try:
    import tkinter as tk
    from tkinter import messagebox, ttk
    from PIL import Image, ImageDraw, ImageFont, ImageTk
except ImportError as exc:  # pragma: no cover - depends on local Python install
    print(f"需要 Pillow 和 Tkinter 才能启动预览器: {exc}", file=sys.stderr)
    raise SystemExit(2)


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_STORY_DIR = ROOT / "assets" / "stories" / "odyssey_homecoming"
FVID_HEADER = 16
FVID_RECORD = 5004
HINT_NAMES = {0: "PARTIAL", 1: "FULL", 2: "FINAL_FULL"}


@dataclass
class Scene:
    index: int
    title: str
    narration: str
    image_ref: str
    audio_ref: str
    image_path: Path
    audio_path: Path
    audio_duration: float
    hint: int


class StoryPackage:
    def __init__(self, story_dir: Path) -> None:
        self.story_dir = story_dir.resolve()
        self.story_path = self.story_dir / "story.json"
        self.story = json.loads(self.story_path.read_text(encoding="utf-8"))
        self.scenes: list[Scene] = []
        self.frames: list[tuple[int, bytes]] = []
        self.errors: list[str] = []
        self.warnings: list[str] = []
        self._read_fvid()
        self._read_scenes()
        self._validate()

    def _read_fvid(self) -> None:
        ref = self.story.get("fvid", "")
        path = self.story_dir / ref
        if not path.is_file():
            self.errors.append(f"缺少 FVID: {ref}")
            return
        raw = path.read_bytes()
        if len(raw) < FVID_HEADER:
            self.errors.append("FVID 文件太小")
            return
        magic, version, _, width, height, count, _ = struct.unpack_from(
            "<4sBBHHH4s", raw, 0
        )
        if (magic, version, width, height) != (b"FVID", 1, 200, 200):
            self.errors.append(
                f"FVID 头无效: {magic!r} v{version} {width}x{height}"
            )
            return
        expected = FVID_HEADER + count * FVID_RECORD
        if len(raw) != expected:
            self.errors.append(f"FVID 长度错误: 实际 {len(raw)}，应为 {expected}")
            return
        for index in range(count):
            offset = FVID_HEADER + index * FVID_RECORD
            hint = raw[offset]
            self.frames.append((hint, raw[offset + 4:offset + FVID_RECORD]))

    def _read_scenes(self) -> None:
        for item in self.story.get("scenes", []):
            index = int(item.get("index", -1))
            image_ref = str(item.get("image", ""))
            audio_ref = str(item.get("audio", ""))
            image_path = self.story_dir / image_ref
            audio_path = self.story_dir / audio_ref
            duration = 0.0
            if audio_path.is_file():
                try:
                    with wave.open(str(audio_path), "rb") as audio:
                        channels = audio.getnchannels()
                        sample_width = audio.getsampwidth()
                        sample_rate = audio.getframerate()
                        duration = audio.getnframes() / max(1, sample_rate)
                        if (channels, sample_width, sample_rate) != (1, 2, 16000):
                            self.errors.append(
                                f"场景 {index:03d} WAV 格式是 "
                                f"{channels}ch/{sample_width * 8}bit/{sample_rate}Hz，"
                                "应为 mono/16-bit/16000Hz"
                            )
                except wave.Error:
                    self.errors.append(f"场景 {index:03d} WAV 无法读取: {audio_ref}")
            hint = self.frames[index][0] if 0 <= index < len(self.frames) else 1
            self.scenes.append(Scene(
                index=index,
                title=str(item.get("title", "")),
                narration=str(item.get("narration", "")),
                image_ref=image_ref,
                audio_ref=audio_ref,
                image_path=image_path,
                audio_path=audio_path,
                audio_duration=duration,
                hint=hint,
            ))

    def _validate(self) -> None:
        if len(self.frames) != len(self.scenes):
            self.errors.append(
                f"场景数 {len(self.scenes)} 与 FVID 帧数 {len(self.frames)} 不一致"
            )
        for expected, scene in enumerate(self.scenes):
            if scene.index != expected:
                self.errors.append(f"场景编号不连续: 位置 {expected} 是 {scene.index}")
            if not scene.image_path.is_file():
                self.errors.append(f"场景 {scene.index:03d} 缺少图片: {scene.image_ref}")
            if not scene.audio_path.is_file():
                self.errors.append(f"场景 {scene.index:03d} 缺少音频: {scene.audio_ref}")
            elif scene.audio_duration <= 0:
                self.errors.append(f"场景 {scene.index:03d} 音频为空或无法读取")
            expected_audio = f"audio/{scene.index:03d}.wav"
            if scene.audio_ref.replace("\\", "/") != expected_audio:
                self.warnings.append(
                    f"场景 {scene.index:03d} 的 story.json 音频引用是 {scene.audio_ref}，"
                    f"固件当前按编号寻找 {expected_audio}"
                )

    def frame_image(self, index: int) -> Image.Image:
        if index < 0 or index >= len(self.frames):
            image = Image.new("L", (200, 200), 255)
            draw = ImageDraw.Draw(image)
            centered_text(draw, (100, 82), "NO FVID FRAME", self._font(12))
            centered_text(draw, (100, 108), f"SCENE {index:02d}", self._font(10))
            return image
        _, raw = self.frames[index]
        pixels = bytearray(200 * 200)
        for y in range(200):
            row = y * 25
            target = y * 200
            for x in range(200):
                pixels[target + x] = 0 if (raw[row + x // 8] & (0x80 >> (x & 7))) == 0 else 255
        return Image.frombytes("L", (200, 200), bytes(pixels))


def centered_text(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str, font: ImageFont.FreeTypeFont) -> None:
    box = draw.textbbox((0, 0), text, font=font)
    draw.text((xy[0] - (box[2] - box[0]) // 2, xy[1]), text, fill=0, font=font)


class OdysseyPreview:
    def __init__(self, package: StoryPackage) -> None:
        self.package = package
        self.root = tk.Tk()
        self.root.title("奥德赛 · 图文音时序预览")
        self.root.geometry("1180x760")
        self.root.minsize(980, 650)
        self.started = time.monotonic()
        self.current = -1
        self.auto = False
        self.closing_shown = False
        self.sequence_token = 0
        self.jobs: list[str] = []
        self.photo: ImageTk.PhotoImage | None = None
        self.font_path = self._find_font()
        self.refresh_mode = tk.StringVar(value="按 FVID 提示")
        self.status = tk.StringVar()
        self._build_ui()
        self._show_opening(play=False)
        self._set_status()
        self.root.bind("<Left>", lambda _: self._manual_scene(self.current - 1))
        self.root.bind("<Right>", lambda _: self._manual_scene(self.current + 1))
        self.root.bind("<space>", lambda _: self._play_current())

    def _find_font(self) -> str | None:
        for name in ("msyh.ttc", "simhei.ttf", "simsun.ttc"):
            path = Path("C:/Windows/Fonts") / name
            if path.is_file():
                return str(path)
        return None

    def _font(self, size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
        if self.font_path:
            return ImageFont.truetype(self.font_path, size)
        return ImageFont.load_default()

    def _build_ui(self) -> None:
        outer = ttk.Frame(self.root, padding=10)
        outer.pack(fill="both", expand=True)

        top = ttk.Frame(outer)
        top.pack(fill="x", pady=(0, 8))
        ttk.Label(top, text="奥德赛 · 图文音时序预览", font=("Segoe UI", 14, "bold")).pack(side="left")
        ttk.Label(top, textvariable=self.status).pack(side="right")

        body = ttk.Frame(outer)
        body.pack(fill="both", expand=True)

        left = ttk.LabelFrame(body, text="场景", padding=6)
        left.pack(side="left", fill="y")
        self.scene_list = tk.Listbox(left, width=27, height=28, exportselection=False)
        self.scene_list.pack(fill="y", expand=True)
        for scene in self.package.scenes:
            self.scene_list.insert("end", f"{scene.index:02d}  {scene.title}")
        self.scene_list.bind("<<ListboxSelect>>", self._list_selected)

        center = ttk.LabelFrame(body, text="FVID 实际帧（200×200）", padding=8)
        center.pack(side="left", fill="both", expand=True, padx=8)
        self.preview = ttk.Label(center, anchor="center")
        self.preview.pack(fill="both", expand=True)

        right = ttk.LabelFrame(body, text="场景信息", padding=8)
        right.pack(side="right", fill="both", expand=True)
        self.info = tk.Text(right, width=48, height=16, wrap="word", state="disabled")
        self.info.pack(fill="both", expand=True)

        controls = ttk.Frame(right)
        controls.pack(fill="x", pady=(8, 4))
        ttk.Button(controls, text="开幕", command=self._manual_opening).pack(side="left")
        ttk.Button(controls, text="上一幕", command=lambda: self._manual_scene(self.current - 1)).pack(side="left", padx=3)
        ttk.Button(controls, text="下一幕", command=lambda: self._manual_scene(self.current + 1)).pack(side="left", padx=3)
        ttk.Button(controls, text="播放本幕", command=self._play_current).pack(side="left", padx=3)
        ttk.Button(controls, text="停止声音", command=self._stop_audio).pack(side="left", padx=3)

        refresh = ttk.Frame(right)
        refresh.pack(fill="x", pady=(0, 6))
        ttk.Label(refresh, text="刷新模拟:").pack(side="left")
        ttk.Combobox(refresh, textvariable=self.refresh_mode, state="readonly", width=17,
                     values=("按 FVID 提示", "不等待", "局刷 400ms", "全刷 1755ms")).pack(side="left", padx=5)
        ttk.Button(refresh, text="自动播放", command=self._start_auto).pack(side="left")

        log_frame = ttk.LabelFrame(right, text="顺序日志", padding=4)
        log_frame.pack(fill="both", expand=True)
        self.log = tk.Text(log_frame, height=10, wrap="none", state="disabled", font=("Consolas", 9))
        self.log.pack(fill="both", expand=True)

        bottom = ttk.Frame(outer)
        bottom.pack(fill="x", pady=(8, 0))
        result = "PASS" if not self.package.errors else f"FAIL {len(self.package.errors)}"
        ttk.Label(bottom, text=f"资源检查: {result}    警告: {len(self.package.warnings)}").pack(side="left")
        ttk.Button(bottom, text="显示检查结果", command=self._show_validation).pack(side="right")

    def _set_status(self) -> None:
        if self.current < 0:
            self.status.set("开幕卡")
        else:
            scene = self.package.scenes[self.current]
            self.status.set(f"场景 {scene.index:02d}/{len(self.package.scenes) - 1:02d}")

    def _write_info(self, text: str) -> None:
        self.info.configure(state="normal")
        self.info.delete("1.0", "end")
        self.info.insert("1.0", text)
        self.info.configure(state="disabled")

    def _log(self, text: str) -> None:
        elapsed = time.monotonic() - self.started
        self.log.configure(state="normal")
        self.log.insert("end", f"+{elapsed:8.3f}s  {text}\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _cancel_jobs(self) -> None:
        for job in self.jobs:
            try:
                self.root.after_cancel(job)
            except tk.TclError:
                pass
        self.jobs.clear()
        self.sequence_token += 1

    def _schedule(self, delay_ms: int, callback: Callable[[], None]) -> None:
        self.jobs.append(self.root.after(delay_ms, callback))

    def _render_image(self, image: Image.Image) -> None:
        image = image.resize((600, 600), Image.Resampling.NEAREST)
        self.photo = ImageTk.PhotoImage(image)
        self.preview.configure(image=self.photo)

    def _card(self, title: str, subtitle: str) -> Image.Image:
        image = Image.new("L", (200, 200), 255)
        draw = ImageDraw.Draw(image)
        draw.line((24, 48, 176, 48), fill=0)
        centered_text(draw, (100, 70), title, self._font(18))
        centered_text(draw, (100, 105), subtitle, self._font(10))
        draw.line((24, 132, 176, 132), fill=0)
        return image

    def _show_opening(self, play: bool) -> None:
        self._cancel_jobs()
        self._stop_audio()
        self.current = -1
        self.auto = False
        self.closing_shown = False
        story = self.package.story
        title = story.get("title", "THE ODYSSEY")
        author = story.get("opening_display", {}).get("author", story.get("author", ""))
        self._render_image(self._card(title, author))
        self._write_info(f"开幕卡\n\n标题: {title}\n作者: {author}\n\n开幕音频: {story.get('opening_audio', '')}\n时长: {story.get('opening_duration_seconds', '?')} 秒")
        self.scene_list.selection_clear(0, "end")
        self._set_status()
        self._log("display opening_card")
        if play:
            self._play_opening()

    def _play_opening(self) -> None:
        self._cancel_jobs()
        self._stop_audio()
        self.auto = True
        token = self.sequence_token
        delay = self._refresh_ms(1)
        self._log(f"display request opening_card hint=FULL")
        self._schedule(delay, lambda: self._opening_refresh_done(token))

    def _opening_refresh_done(self, token: int) -> None:
        if token != self.sequence_token or not self.auto:
            return
        ref = self.package.story.get("opening_audio", "")
        path = self.package.story_dir / ref
        self._log(f"refresh complete opening_card simulated={self._refresh_ms(1)}ms")
        if self._play_audio(path):
            self._log(f"audio start opening path={ref}")
        else:
            self._log(f"audio unavailable opening path={ref}")
        duration = float(self.package.story.get("opening_duration_seconds", 0))
        self._schedule(max(1, int(duration * 1000)), lambda: self._auto_scene(0, token))

    def _refresh_ms(self, hint: int) -> int:
        mode = self.refresh_mode.get()
        if mode == "不等待":
            return 0
        if mode == "局刷 400ms":
            return 400
        if mode == "全刷 1755ms":
            return 1755
        return 1755 if hint != 0 else 400

    def _show_scene(self, index: int, auto: bool) -> None:
        if not self.package.scenes:
            return
        index = max(0, min(index, len(self.package.scenes) - 1))
        self._cancel_jobs()
        self._stop_audio()
        self.auto = auto
        self.closing_shown = False
        self.current = index
        scene = self.package.scenes[index]
        hint = scene.hint
        self._render_image(self.package.frame_image(index))
        self._write_info(
            f"场景 {scene.index:02d}\n\n"
            f"标题: {scene.title}\n\n"
            f"旁白:\n{scene.narration}\n\n"
            f"图片: {scene.image_ref}\n"
            f"音频: {scene.audio_ref}\n"
            f"音频时长: {scene.audio_duration:.3f} 秒\n"
            f"FVID 刷新提示: {HINT_NAMES.get(hint, f'UNKNOWN({hint})')}"
        )
        self.scene_list.selection_clear(0, "end")
        self.scene_list.selection_set(index)
        self.scene_list.see(index)
        self._set_status()
        token = self.sequence_token
        delay = self._refresh_ms(hint)
        self._log(f"display request scene={index:02d} hint={HINT_NAMES.get(hint, hint)}")
        self._schedule(delay, lambda: self._refresh_done(index, auto, token))

    def _refresh_done(self, index: int, auto: bool, token: int) -> None:
        if token != self.sequence_token or index != self.current:
            return
        scene = self.package.scenes[index]
        self._log(f"refresh complete scene={index:02d} simulated={self._refresh_ms(scene.hint)}ms")
        if self._play_audio(scene.audio_path):
            self._log(f"audio start scene={index:02d} path={scene.audio_ref}")
        else:
            self._log(f"audio unavailable scene={index:02d} path={scene.audio_ref}")
        if not auto:
            return
        if index == len(self.package.scenes) - 1:
            closing_delay = max(0.0, scene.audio_duration - 3.0)
            self._schedule(int(closing_delay * 1000), lambda: self._show_closing(token))
            self._schedule(max(1, int(scene.audio_duration * 1000)), lambda: self._auto_finished(token))
        else:
            self._schedule(max(1, int(scene.audio_duration * 1000)), lambda: self._auto_scene(index + 1, token))

    def _show_closing(self, token: int) -> None:
        if token != self.sequence_token or self.current < 0:
            return
        self.closing_shown = True
        story = self.package.story
        overlay = story.get("closing_overlay", {})
        self._render_image(self._card(overlay.get("text", "OVER"), f"WRITER: {overlay.get('screenwriter', '')}"))
        self._log("display closing_overlay remaining=3.000s")

    def _auto_scene(self, index: int, token: int) -> None:
        if token != self.sequence_token or not self.auto:
            return
        self._show_scene(index, auto=True)

    def _auto_finished(self, token: int) -> None:
        if token != self.sequence_token:
            return
        self._stop_audio()
        self.auto = False
        self._log("sequence complete")

    def _play_audio(self, path: Path) -> bool:
        if not path.is_file():
            return False
        if winsound is None:
            return False
        winsound.PlaySound(str(path), winsound.SND_FILENAME | winsound.SND_ASYNC)
        return True

    def _stop_audio(self) -> None:
        if winsound is not None:
            winsound.PlaySound(None, 0)

    def _play_current(self) -> None:
        if self.current < 0:
            self._play_opening()
            return
        self._stop_audio()
        scene = self.package.scenes[self.current]
        if self._play_audio(scene.audio_path):
            self._log(f"audio replay scene={scene.index:02d} path={scene.audio_ref}")
        else:
            self._log(f"audio unavailable scene={scene.index:02d}")

    def _manual_opening(self) -> None:
        self._show_opening(play=True)

    def _manual_scene(self, index: int) -> None:
        if not self.package.scenes:
            return
        self._show_scene(index, auto=False)

    def _start_auto(self) -> None:
        self._show_opening(play=True)

    def _list_selected(self, _event: object) -> None:
        selection = self.scene_list.curselection()
        if selection:
            self._manual_scene(int(selection[0]))

    def _show_validation(self) -> None:
        lines = []
        lines.extend("FAIL " + item for item in self.package.errors)
        lines.extend("WARN " + item for item in self.package.warnings)
        if not lines:
            lines.append("PASS 图文音引用、FVID 帧数和音频文件均可读取")
        messagebox.showinfo("资源检查结果", "\n".join(lines))

    def run(self) -> None:
        self.root.mainloop()


def main() -> None:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="Odyssey story image/text/audio timing previewer")
    parser.add_argument("--story-dir", type=Path, default=DEFAULT_STORY_DIR,
                        help="story package directory; default is assets/stories/odyssey_homecoming")
    parser.add_argument("--check", action="store_true", help="validate assets without opening the window")
    args = parser.parse_args()
    package = StoryPackage(args.story_dir)
    for item in package.errors:
        print("FAIL", item)
    for item in package.warnings:
        print("WARN", item)
    if not package.errors:
        print(f"PASS story={package.story.get('id', '')} scenes={len(package.scenes)} fvid_frames={len(package.frames)}")
    if args.check:
        raise SystemExit(1 if package.errors else 0)
    OdysseyPreview(package).run()


if __name__ == "__main__":
    main()
