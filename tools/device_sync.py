#!/usr/bin/env python3
"""Small dependency-free client for the ESP32 file-sync HTTP service."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import secrets
import socket
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


DISCOVERY_PORT = 4210
CHUNK_SIZE = 1436
CONFIG_PATH = Path.home() / ".device_sync.json"


class DeviceError(RuntimeError):
    pass


class DeviceSync:
    def __init__(self, base_url: str, token: str | None = None):
        self.base_url = base_url.rstrip("/")
        self.token = token

    def request(self, method: str, path: str, body: bytes | str | None = None,
                headers: dict[str, str] | None = None, raw: bool = False):
        request_headers = {"Accept": "application/json"}
        if self.token:
            request_headers["X-File-Sync-Token"] = self.token
        if headers:
            request_headers.update(headers)
        if isinstance(body, str):
            body = body.encode()
        request = urllib.request.Request(self.base_url + path, data=body,
                                         headers=request_headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                data = response.read()
                if raw:
                    return data
                return json.loads(data.decode() or "{}")
        except urllib.error.HTTPError as error:
            payload = error.read().decode(errors="replace")
            try:
                detail = json.loads(payload)
            except json.JSONDecodeError:
                detail = payload
            raise DeviceError(f"HTTP {error.code}: {detail}") from error
        except urllib.error.URLError as error:
            raise DeviceError(f"connection failed: {error.reason}") from error

    def device(self):
        return self.request("GET", "/api/v1/device")

    def settings(self, values: dict | None = None):
        if values is None:
            return self.request("GET", "/api/v1/settings")
        return self.request("POST", "/api/v1/settings", json.dumps(values),
                            {"Content-Type": "application/json"})

    def files(self, path: str = "/"):
        query = urllib.parse.urlencode({"path": path})
        return self.request("GET", f"/api/v1/files?{query}")

    def upload(self, local: Path, remote: str):
        size = local.stat().st_size
        digest = sha256_file(local)
        with local.open("rb") as source:
            offset = 0
            while True:
                chunk = source.read(CHUNK_SIZE)
                if not chunk:
                    break
                end = offset + len(chunk) - 1
                self.request("PUT", "/api/v1/file?" + urllib.parse.urlencode({"path": remote}),
                             chunk, {"Content-Range": f"bytes {offset}-{end}/{size}",
                                     "X-File-Path": remote, "X-SHA256": digest})
                offset = end + 1
        print(f"uploaded {remote} ({size} bytes, sha256 {digest})")

    def download(self, remote: str, local: Path):
        metadata = self.files(parent_path(remote))
        entries = [entry for entry in metadata.get("files", []) if entry.get("path") == remote]
        if not entries:
            raise DeviceError(f"remote file not found: {remote}")
        size = int(entries[0]["size"])
        local.parent.mkdir(parents=True, exist_ok=True)
        with local.open("wb") as output:
            offset = 0
            while offset < size:
                end = min(size - 1, offset + CHUNK_SIZE * 16 - 1)
                query = urllib.parse.urlencode({"path": remote})
                data = self.request("GET", f"/api/v1/file?{query}",
                                    headers={"Range": f"bytes={offset}-{end}"}, raw=True)
                if not data:
                    raise DeviceError(f"empty download response at offset {offset}")
                output.write(data)
                offset += len(data)
        digest = sha256_file(local)
        print(f"downloaded {remote} -> {local} ({size} bytes, sha256 {digest})")

    def sync_directory(self, local_root: Path, remote_root: str):
        files = []
        for path in sorted(p for p in local_root.rglob("*") if p.is_file()):
            relative = path.relative_to(local_root).as_posix()
            files.append({"path": relative, "size": path.stat().st_size,
                          "sha256": sha256_file(path)})
        if not files:
            raise DeviceError("local directory contains no files")
        transaction = f"sync-{time.strftime('%Y%m%d-%H%M%S')}-{secrets.token_hex(2)}"
        prepared = self.request("POST", "/api/v1/sync/prepare",
                                json.dumps({"transaction": transaction,
                                            "root": remote_root, "files": files}),
                                {"Content-Type": "application/json"})
        print(f"prepared {prepared.get('transaction', transaction)} ({len(files)} files)")
        try:
            for item in files:
                relative = item["path"]
                query = urllib.parse.urlencode({"transaction": transaction, "path": relative})
                status = self.request("GET", f"/api/v1/sync/status?{query}")
                offset = int(status.get("received", 0))
                path = local_root / Path(relative)
                with path.open("rb") as source:
                    source.seek(offset)
                    while offset < item["size"]:
                        chunk = source.read(CHUNK_SIZE)
                        if not chunk:
                            raise DeviceError(f"local file ended early: {relative}")
                        end = offset + len(chunk) - 1
                        self.request("PUT", "/api/v1/sync/chunk?" + query, chunk,
                                     {"Content-Range": f"bytes {offset}-{end}/{item['size']}",
                                      "X-Sync-Transaction": transaction,
                                      "X-Sync-Path": relative})
                        offset = end + 1
                print(f"  {relative}: {offset}/{item['size']}")
            result = self.request("POST", "/api/v1/sync/commit", "{}",
                                  {"Content-Type": "application/json"})
            print(f"committed {result}")
        except Exception:
            try:
                self.request("POST", "/api/v1/sync/abort", "{}",
                             {"Content-Type": "application/json"})
            except DeviceError:
                pass
            raise

    def delete(self, remote: str, recursive: bool = False):
        query = urllib.parse.urlencode({"path": remote, "confirm": "true",
                                         "recursive": "true" if recursive else "false"})
        print(self.request("DELETE", f"/api/v1/file?{query}"))

    def rename(self, source: str, destination: str):
        print(self.request("POST", "/api/v1/rename",
                           json.dumps({"from": source, "to": destination}),
                           {"Content-Type": "application/json"}))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parent_path(path: str) -> str:
    path = path.rstrip("/") or "/"
    parent = path.rsplit("/", 1)[0]
    return parent or "/"


def discover(timeout: float = 2.0) -> list[dict]:
    request = b"FILE_SYNC_DISCOVER\n"
    found: dict[str, dict] = {}
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.settimeout(0.25)
        sock.bind(("", 0))
        sock.sendto(request, ("255.255.255.255", DISCOVERY_PORT))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data, address = sock.recvfrom(1024)
            except socket.timeout:
                continue
            try:
                item = json.loads(data.decode())
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            item.setdefault("ip", address[0])
            found[item.get("id", address[0])] = item
    return list(found.values())


def load_config() -> dict:
    try:
        return json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def save_config(config: dict):
    CONFIG_PATH.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")


def client_from_args(args, require_token: bool = True) -> DeviceSync:
    config = load_config()
    base_url = args.url or config.get("url")
    if not base_url:
        devices = discover()
        if not devices:
            raise DeviceError("no file-sync device discovered; pass --url")
        device = devices[0]
        base_url = f"http://{device['ip']}:{device.get('port', 80)}"
        print(f"using {device.get('name', device.get('id', '?'))} at {base_url}", file=sys.stderr)
    token = args.token or config.get("token")
    if require_token and not token:
        raise DeviceError("token required; show it in Settings > WIFI + SYNC or run pair")
    return DeviceSync(base_url, token)


def command_discover(_args):
    devices = discover()
    print(json.dumps(devices, indent=2))


def command_pair(args):
    client = client_from_args(args, require_token=False)
    if not args.token:
        raise DeviceError("pair requires --token from the device Settings screen")
    client.token = args.token
    client.files("/")
    config = load_config()
    config.update({"url": client.base_url, "token": args.token})
    save_config(config)
    print(f"paired {client.base_url}; credentials saved to {CONFIG_PATH}")


def command_configure(args):
    values = {}
    if args.name is not None:
        values["name"] = args.name
    for key in ("auto_sync", "allow_delete", "auto_story_scan"):
        value = getattr(args, key)
        if value is not None:
            values[key] = value
    if args.rotate_token:
        values["rotate_token"] = True
    if not values:
        print(json.dumps(client_from_args(args).settings(), indent=2))
        return
    result = client_from_args(args).settings(values)
    print(json.dumps(result, indent=2))
    if result.get("token"):
        config = load_config()
        config["token"] = result["token"]
        save_config(config)


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", help="device URL, e.g. http://192.168.1.42")
    parser.add_argument("--token", help="32-character token from Settings")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("discover")
    sub.add_parser("device")
    pair = sub.add_parser("pair")
    pair.add_argument("--token", required=True)
    listing = sub.add_parser("list")
    listing.add_argument("path", nargs="?", default="/")
    upload = sub.add_parser("upload")
    upload.add_argument("local", type=Path)
    upload.add_argument("remote")
    download = sub.add_parser("download")
    download.add_argument("remote")
    download.add_argument("local", type=Path)
    sync = sub.add_parser("sync")
    sync.add_argument("local", type=Path)
    sync.add_argument("remote_root")
    delete = sub.add_parser("delete")
    delete.add_argument("remote")
    delete.add_argument("--recursive", action="store_true")
    rename = sub.add_parser("rename")
    rename.add_argument("source")
    rename.add_argument("destination")
    configure = sub.add_parser("configure")
    configure.add_argument("--name")
    configure.add_argument("--auto-sync", dest="auto_sync", action=argparse.BooleanOptionalAction, default=None)
    configure.add_argument("--allow-delete", dest="allow_delete", action=argparse.BooleanOptionalAction, default=None)
    configure.add_argument("--auto-story-scan", dest="auto_story_scan", action=argparse.BooleanOptionalAction, default=None)
    configure.add_argument("--rotate-token", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if args.command == "discover":
            command_discover(args)
            return 0
        if args.command == "pair":
            command_pair(args)
            return 0
        if args.command == "configure":
            command_configure(args)
            return 0
        client = client_from_args(args, require_token=args.command != "device")
        if args.command == "device":
            print(json.dumps(client.device(), indent=2))
        elif args.command == "list":
            print(json.dumps(client.files(args.path), indent=2))
        elif args.command == "upload":
            client.upload(args.local, args.remote)
        elif args.command == "download":
            client.download(args.remote, args.local)
        elif args.command == "sync":
            client.sync_directory(args.local, args.remote_root)
        elif args.command == "delete":
            client.delete(args.remote, args.recursive)
        elif args.command == "rename":
            client.rename(args.source, args.destination)
        return 0
    except (DeviceError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
