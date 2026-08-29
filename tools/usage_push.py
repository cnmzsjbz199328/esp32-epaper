#!/usr/bin/env python3
"""Push quota-only Claude Code and Codex snapshots to the epaper app."""

import argparse
import asyncio
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, Optional
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

COMMAND_UUID = "7e0d0003-7b83-4b74-9d5c-6b5851120001"
RESPONSE_UUID = "7e0d0004-7b83-4b74-9d5c-6b5851120001"
DEVICE_PREFIX = "ECO-epaper_154-"
DEFAULT_INTERVAL = 21600
OPPORTUNISTIC_MIN = 1800


def truncate_utf8(value: str, limit: int = 63) -> str:
    return (value or "").encode("utf-8")[:limit].decode("utf-8", errors="ignore")


def epoch_from_iso(value: Any) -> Optional[int]:
    if not value:
        return None
    try:
        return int(datetime.fromisoformat(str(value).replace("Z", "+00:00")).timestamp())
    except (TypeError, ValueError, OverflowError):
        return None


def minutes_until(epoch: Optional[int]) -> int:
    return max(0, int((epoch - time.time()) / 60)) if epoch is not None else 0


def parse_human_reset(value: str) -> Optional[int]:
    match = re.search(r"([A-Za-z]{3}\s+\d{1,2},\s+\d{1,2}:\d{2}\s*[ap]m)", value, re.I)
    if not match:
        return None
    text = re.sub(r"\s+", " ", match.group(1))
    now = datetime.now().astimezone()
    for year in (now.year, now.year + 1):
        for fmt in ("%b %d, %I:%M%p", "%b %d, %I:%M %p"):
            try:
                parsed = datetime.strptime(f"{text} {year}", f"{fmt} %Y").replace(tzinfo=now.tzinfo)
                if parsed.timestamp() >= time.time() - 86400:
                    return int(parsed.timestamp())
            except ValueError:
                pass
    return None


def parse_claude_result(result: str) -> Optional[Dict[str, int]]:
    session = re.search(r"Current\s+session:\s*(\d+)\s*%\s*used\s*[·•]\s*resets\s*([^\r\n]+)", result, re.I)
    week = re.search(r"Current\s+week\s*\(all models\):\s*(\d+)\s*%\s*used\s*[·•]\s*resets\s*([^\r\n]+)", result, re.I)
    if not session or not week:
        return None
    sr = parse_human_reset(session.group(2))
    wr = parse_human_reset(week.group(2))
    if sr is None or wr is None:
        return None
    return {"s": min(100, int(session.group(1))), "sr": minutes_until(sr),
            "w": min(100, int(week.group(1))), "wr": minutes_until(wr)}


def load_json(path: Path) -> Optional[Dict[str, Any]]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle)
        return value if isinstance(value, dict) else None
    except (OSError, ValueError):
        return None


def claude_credentials() -> Optional[Dict[str, Any]]:
    path = Path(os.environ.get("USERPROFILE", str(Path.home()))) / ".claude" / ".credentials.json"
    return load_json(path)


def claude_cc_status() -> Dict[str, str]:
    path = Path(os.environ.get("USERPROFILE", str(Path.home()))) / ".clawd" / "cc_status.json"
    value = load_json(path) or {}
    state = str(value.get("state", value.get("status", "")))
    message = str(value.get("message", value.get("msg", "")))
    return {"cc": state, "ccm": truncate_utf8(message)} if state else {}


class Provider:
    name = ""

    def collect(self) -> Optional[Dict[str, Any]]:
        raise NotImplementedError

    def close(self) -> None:
        pass


class ClaudeProvider(Provider):
    name = "claude"

    def __init__(self, source: str = "usage") -> None:
        self.source = source

    def _metadata(self) -> Dict[str, str]:
        credentials = claude_credentials() or {}
        oauth = credentials.get("claudeAiOauth", {})
        tier = oauth.get("subscriptionType", "") if isinstance(oauth, dict) else ""
        return {"acct": str(tier)[:7]} if tier else {}

    def _oauth(self) -> Optional[Dict[str, Any]]:
        credentials = claude_credentials() or {}
        oauth = credentials.get("claudeAiOauth", {})
        token = oauth.get("accessToken") if isinstance(oauth, dict) else None
        if not token:
            return None
        request = Request("https://api.anthropic.com/api/oauth/usage",
                          headers={"Authorization": f"Bearer {token}",
                                   "anthropic-beta": "oauth-2025-04-20"})
        try:
            with urlopen(request, timeout=15) as response:
                data = json.loads(response.read().decode("utf-8"))
            five = data.get("five_hour") or {}
            seven = data.get("seven_day") or {}
            if not isinstance(five, dict) or not isinstance(seven, dict):
                return None
            return {"s": max(0, min(100, int(float(five.get("utilization", 0))))),
                    "sr": minutes_until(epoch_from_iso(five.get("resets_at"))),
                    "w": max(0, min(100, int(float(seven.get("utilization", 0))))),
                    "wr": minutes_until(epoch_from_iso(seven.get("resets_at")))}
        except (HTTPError, URLError, OSError, ValueError, TypeError):
            return None

    def collect(self) -> Optional[Dict[str, Any]]:
        values = None
        if self.source == "usage":
            try:
                completed = subprocess.run(
                    ["claude", "-p", "/usage", "--output-format", "json", "--no-session-persistence"],
                    capture_output=True, text=True, timeout=45, check=False)
                envelope = json.loads(completed.stdout)
                result = envelope.get("result", "")
                if completed.returncode == 0 and isinstance(result, str):
                    values = parse_claude_result(result)
            except (OSError, subprocess.SubprocessError, ValueError):
                pass
        if values is None:
            values = self._oauth()
        if values is None:
            return None
        values.update({"p": self.name, "st": "allowed", "ok": True})
        values.update(self._metadata())
        values.update(claude_cc_status())
        return values


def merge_dict(base: Dict[str, Any], update: Dict[str, Any]) -> Dict[str, Any]:
    result = dict(base)
    for key, value in update.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = merge_dict(result[key], value)
        elif value is not None:
            result[key] = value
    return result


def map_rate_limits(limits: Dict[str, Any], snake: bool = False) -> Optional[Dict[str, Any]]:
    primary = limits.get("primary") or {}
    secondary = limits.get("secondary") or {}
    used_key, reset_key = (("used_percent", "resets_at") if snake else ("usedPercent", "resetsAt"))
    if not isinstance(primary, dict) or not isinstance(secondary, dict):
        return None
    try:
        values = {"p": "codex", "s": max(0, min(100, int(float(primary.get(used_key, 0))))),
                  "sr": minutes_until(int(primary[reset_key]) if primary.get(reset_key) else None),
                  "w": max(0, min(100, int(float(secondary.get(used_key, 0))))),
                  "wr": minutes_until(int(secondary[reset_key]) if secondary.get(reset_key) else None),
                  "st": "allowed", "ok": True}
        if snake:
            values["acct"] = str(limits.get("plan_type", ""))[:7]
            limited, rejected = bool(limits.get("spend_control_reached")), bool(limits.get("rate_limit_reached_type"))
        else:
            values["acct"] = str(limits.get("planType", ""))[:7]
            limited, rejected = bool(limits.get("spendControlReached")), bool(limits.get("rateLimitReachedType"))
        values["st"] = "limited" if limited else ("rejected" if rejected else "allowed")
        return values
    except (TypeError, ValueError, OverflowError):
        return None


class CodexProvider(Provider):
    name = "codex"

    def __init__(self) -> None:
        self.process = None
        self.lines: "queue.Queue[Dict[str, Any]]" = queue.Queue()
        self.request_id = 0
        self.snapshot = None
        self.raw_limits: Dict[str, Any] = {}

    def _reader(self) -> None:
        if not self.process or not self.process.stdout:
            return
        for line in self.process.stdout:
            try:
                value = json.loads(line)
                if isinstance(value, dict):
                    self.lines.put(value)
            except ValueError:
                pass

    def _send(self, message: Dict[str, Any]) -> None:
        if not self.process or not self.process.stdin:
            raise OSError("codex app-server is not running")
        self.process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
        self.process.stdin.flush()

    def _wait(self, request_id: int) -> Optional[Dict[str, Any]]:
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            try:
                message = self.lines.get(timeout=max(0.05, deadline - time.monotonic()))
            except queue.Empty:
                return None
            if message.get("method") == "account/rateLimits/updated":
                params = message.get("params") or {}
                update = params.get("rateLimits") or params.get("rate_limits")
                if isinstance(update, dict):
                    self.raw_limits = merge_dict(self.raw_limits, update)
                    mapped = map_rate_limits(self.raw_limits)
                    if mapped:
                        self.snapshot = merge_dict(self.snapshot or {}, mapped)
                continue
            if message.get("id") == request_id:
                return message
        return None

    def _start(self) -> bool:
        if self.process and self.process.poll() is None:
            return True
        try:
            self.process = subprocess.Popen(["codex", "app-server", "--listen", "stdio://"],
                                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=subprocess.DEVNULL, text=True, bufsize=1)
        except OSError:
            return False
        threading.Thread(target=self._reader, daemon=True).start()
        self.request_id += 1
        self._send({"method": "initialize", "id": self.request_id,
                    "params": {"clientInfo": {"name": "epaper-usage-push", "title": "Usage Push", "version": "1"}}})
        if not self._wait(self.request_id):
            self.close()
            return False
        self._send({"method": "initialized", "params": {}})
        self.request_id += 1
        self._send({"method": "account/rateLimits/read", "id": self.request_id})
        response = self._wait(self.request_id)
        if not response:
            self.close()
            return False
        result = response.get("result") or {}
        limits = result.get("rateLimits")
        if not isinstance(limits, dict):
            buckets = result.get("rateLimitsByLimitId") or {}
            limits = buckets.get("codex") if isinstance(buckets, dict) else None
        self.raw_limits = dict(limits) if isinstance(limits, dict) else {}
        self.snapshot = map_rate_limits(self.raw_limits) if self.raw_limits else None
        return self.snapshot is not None

    def _fallback(self) -> Optional[Dict[str, Any]]:
        root = Path(os.environ.get("USERPROFILE", str(Path.home()))) / ".codex" / "sessions"
        try:
            files = sorted(root.rglob("rollout-*.jsonl"), key=lambda p: p.stat().st_mtime, reverse=True)
        except OSError:
            return None
        for path in files[:20]:
            try:
                lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
            except OSError:
                continue
            for line in reversed(lines):
                try:
                    value = json.loads(line)
                except ValueError:
                    continue
                payload = value.get("payload") if isinstance(value, dict) else None
                limits = payload.get("rate_limits") if isinstance(payload, dict) else None
                if isinstance(limits, dict):
                    return map_rate_limits(limits, snake=True)
        return None

    def collect(self) -> Optional[Dict[str, Any]]:
        if self.snapshot is None and not self._start():
            self.snapshot = self._fallback()
        return dict(self.snapshot) if self.snapshot else None

    def poll_update(self) -> Optional[Dict[str, Any]]:
        changed = False
        while True:
            try:
                message = self.lines.get_nowait()
            except queue.Empty:
                break
            if message.get("method") != "account/rateLimits/updated":
                continue
            params = message.get("params") or {}
            update = params.get("rateLimits") or params.get("rate_limits")
            if isinstance(update, dict):
                self.raw_limits = merge_dict(self.raw_limits, update)
            mapped = map_rate_limits(self.raw_limits) if self.raw_limits else None
            if mapped:
                self.snapshot = merge_dict(self.snapshot or {}, mapped)
                changed = True
        return dict(self.snapshot) if changed and self.snapshot else None

    def close(self) -> None:
        if not self.process:
            return
        try:
            self.process.terminate()
            self.process.wait(timeout=2)
        except (OSError, subprocess.TimeoutExpired):
            try:
                self.process.kill()
            except OSError:
                pass
        self.process = None
        self.snapshot = None
        self.raw_limits = {}


class UsageSender:
    def __init__(self, client: Any = None, dry_run: bool = False) -> None:
        self.client, self.dry_run, self.rid = client, dry_run, 0

    async def send(self, values: Dict[str, Any]) -> None:
        self.rid += 1
        payload = {"v": 1, "rid": self.rid, "op": "usage.push"}
        payload.update(values)
        raw = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        if len(raw.encode("utf-8")) > 256:
            print(f"[usage] skip {values.get('p', '?')}: payload exceeds 256 bytes", file=sys.stderr)
            return
        print(raw)
        if not self.dry_run:
            await self.client.write_gatt_char(COMMAND_UUID, raw.encode("utf-8"), response=False)


async def scan_for_device() -> Any:
    from bleak import BleakScanner
    for device in await BleakScanner.discover(timeout=10):
        if (device.name or "").startswith(DEVICE_PREFIX):
            return device
    return None


async def collect_and_send(sender: UsageSender, providers: list[Provider]) -> None:
    for provider in providers:
        values = provider.collect()
        if values is None:
            print(f"[usage] {provider.name}: no snapshot", file=sys.stderr)
            continue
        await sender.send(values)
        await asyncio.sleep(0.2)


async def run_live(args: argparse.Namespace, providers: list[Provider]) -> None:
    from bleak import BleakClient
    while True:
        device = await scan_for_device()
        if not device:
            print("[usage] waiting for ECO-epaper_154 device", file=sys.stderr)
            await asyncio.sleep(10)
            continue
        try:
            async with BleakClient(device, timeout=20) as client:
                if getattr(client, "mtu_size", 0) and client.mtu_size < 200:
                    print(f"[usage] warning: negotiated MTU is {client.mtu_size}", file=sys.stderr)
                await client.start_notify(RESPONSE_UUID, lambda *_: None)
                sender = UsageSender(client)
                await collect_and_send(sender, providers)
                if args.once:
                    return
                next_round = time.monotonic() + args.interval
                last_codex_push = time.monotonic()
                while True:
                    if time.monotonic() >= next_round:
                        await collect_and_send(sender, providers)
                        next_round = time.monotonic() + args.interval
                    codex = next((p for p in providers if isinstance(p, CodexProvider)), None)
                    if codex and not args.no_opportunistic:
                        update = codex.poll_update()
                        if update and time.monotonic() - last_codex_push >= OPPORTUNISTIC_MIN:
                            await sender.send(update)
                            last_codex_push = time.monotonic()
                    await asyncio.sleep(1)
        except Exception as exc:
            print(f"[usage] disconnected: {type(exc).__name__}", file=sys.stderr)
            await asyncio.sleep(3)


async def main_async(args: argparse.Namespace) -> int:
    providers: list[Provider] = []
    selected = {part.strip() for part in args.provider.split(",") if part.strip()}
    if "claude" in selected:
        providers.append(ClaudeProvider(args.claude_source))
    if "codex" in selected:
        providers.append(CodexProvider())
    if not providers:
        print("--provider must contain claude and/or codex", file=sys.stderr)
        return 2
    try:
        if args.dry_run:
            await collect_and_send(UsageSender(dry_run=True), providers)
        else:
            await run_live(args, providers)
        return 0
    finally:
        for provider in providers:
            provider.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider", default="claude,codex")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--interval", type=int, default=DEFAULT_INTERVAL)
    parser.add_argument("--claude-source", choices=("usage", "oauth"), default="usage")
    parser.add_argument("--no-opportunistic", action="store_true")
    return parser.parse_args()


if __name__ == "__main__":
    try:
        raise SystemExit(asyncio.run(main_async(parse_args())))
    except KeyboardInterrupt:
        raise SystemExit(130)
