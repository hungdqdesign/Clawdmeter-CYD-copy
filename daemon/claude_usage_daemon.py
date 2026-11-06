#!/usr/bin/env python3
"""Claude Usage Tracker Daemon (BLE) — macOS.

Polls Claude API rate-limit headers and writes a JSON payload to the
ESP32 "Claude Controller" peripheral over a custom GATT service. Uses
bleak (CoreBluetooth backend on macOS). The same payload also carries this
Mac's CPU / GPU / RAM / disk load, so
the display shows quota and machine load on one screen.
"""

import asyncio
import getpass
import glob
import json
import math
import os
import re
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

import httpx
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

try:
    import serial as pyserial   # pyserial — optional; USB path disabled if absent
    _HAVE_SERIAL = True
except ImportError:
    pyserial = None
    _HAVE_SERIAL = False

try:
    import psutil               # optional; system gauges show --% if absent
except ImportError:
    psutil = None

# BLE name of the board. Set CLAUDE_CTRL_NAME in the launchd plist to change it.
DEVICE_NAME = os.environ.get("CLAUDE_CTRL_NAME", "Claude Controller")
# CLAUDE_CTRL_BLE=0 runs USB only — for a board that is always wired to this
# Mac (the CYD has no battery, so it usually is). The USB path doesn't need
# Bluetooth, and a Python without a Bluetooth grant is killed by macOS
# (SIGABRT) the moment it scans, which under launchd's KeepAlive becomes a
# restart loop that also resets the board through the serial port.
BLE_ENABLED = os.environ.get("CLAUDE_CTRL_BLE", "1") != "0"
SERVICE_UUID = "4c41555a-4465-7669-6365-000000000001"
RX_CHAR_UUID = "4c41555a-4465-7669-6365-000000000002"
REQ_CHAR_UUID = "4c41555a-4465-7669-6365-000000000004"

# --- Radio-friendly tuning -------------------------------------------------
# BLE *active scanning* is the single biggest source of 2.4 GHz interference
# with other Bluetooth devices on the same controller (e.g. a BLE mouse). So
# the daemon scans as rarely as possible: once it knows the board's address it
# reconnects directly (no scan), and while the board is unreachable it backs
# off hard instead of hammering the radio (which used to start at 1 s).
CODEX_POLL_INTERVAL = 30
CODEX_STALE = 90
POLL_INTERVAL = 90          # seconds between usage polls (was 60)
TICK = 5
SCAN_TIMEOUT = 6.0          # active-scan burst length (was 8.0)
SCAN_RETRY_MIN = 30         # min seconds between scans when the board is missing
SCAN_RETRY_MAX = 300        # max backoff between scans (was 60)
RECONNECT_DELAY = 8         # pause before reconnecting a dropped (but known) link

# --- USB serial (wired) data path ------------------------------------------
# When the display is plugged into THIS Mac over USB, stream usage to it over
# serial in addition to BLE. The firmware prioritises fresh serial data, so the
# Mac holding the cable owns the screen regardless of who won the BLE race —
# fixes the "two Macs both push over BLE, wrong one wins" problem. Only usbserial
# (CP2102/CH340) ports are probed, so a native-USB display on usbmodem (e.g. a
# separate monitor) is never poked.
SERIAL_BAUD = 115200
# Re-send the latest payload this often (< firmware's 15s TTL). 3s rather than
# the old 8s because each send now also carries fresh CPU/GPU/RAM/disk load.
SERIAL_WRITE_INTERVAL = 3
SERIAL_RESCAN_DELAY = 20     # wait between port-detection sweeps when no display is found
SERIAL_WHOAMI_TOKEN = "CLAWDMETER"
# Payload size ceilings, checked by the test suite against a worst-case sample:
# the firmware's serial line buffer (CMD_BUF_SIZE 384 in main.cpp, minus the
# terminator) and macOS's usual single write-without-response limit over BLE
# (ATT MTU 185 → 182 bytes). A longer payload is truncated and fails to parse.
SERIAL_PAYLOAD_MAX = 383
BLE_PAYLOAD_MAX = 182

# macOS: token lives in Keychain (service "Claude Code-credentials").
# Linux: token lives in ~/.claude/.credentials.json.
KEYCHAIN_SERVICE = "Claude Code-credentials"
CREDENTIALS_PATH = Path.home() / ".claude" / ".credentials.json"
# Cached BLE address. Suffixed per non-default device name so two daemons on
# one host (or a board swap) never reuse each other's stale address.
_ADDR_SUFFIX = "" if DEVICE_NAME == "Claude Controller" else "-" + re.sub(r"\W+", "", DEVICE_NAME).lower()
SAVED_ADDR_FILE = Path.home() / ".config" / "claude-usage-monitor" / ("ble-address" + _ADDR_SUFFIX)

# Subscription usage endpoint (the same one Claude Code's /usage reads). A plain
# GET — consumes no model quota — that returns every rate-limit window at once,
# INCLUDING per-model "weekly_scoped" limits (e.g. Fable 5) that the /v1/messages
# response headers do NOT expose. The `User-Agent: claude-code/*` header is
# REQUIRED — without it this endpoint 429-throttles aggressively.
USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
API_HEADERS_TEMPLATE = {
    "anthropic-version": "2023-06-01",
    "anthropic-beta": "oauth-2025-04-20",
    "Content-Type": "application/json",
    "User-Agent": "claude-code/2.1.5",
}

# ChatGPT Codex usage (the 5th row on the display). The Codex desktop app / CLI
# keeps its OAuth token in ~/.codex/auth.json and refreshes it itself (10-day
# JWT), so we only read it. The usage endpoint is the one Codex's own /status
# reads — a plain GET that consumes no quota.
# ponytail: no token refresh here; if the app is logged out the row shows "--".
CODEX_AUTH_PATH = Path.home() / ".codex" / "auth.json"
CODEX_USAGE_URL = "https://chatgpt.com/backend-api/wham/usage"

# AntiGravity exposes its signed-in account's quota through the app's existing
# language-server listener. This integration ONLY reads that running app: it
# never starts agy/the app, refreshes OAuth, or creates a listener/credential.
# The app reports remainingFraction, so a/ar/ab carry USED %, that same
# bucket's reset countdown, and its group/window (0 Gemini 5h, 1 Gemini 7d,
# 2 Claude+GPT 5h, 3 Claude+GPT 7d, 4/5 known group with unknown cycle).
ANTIGRAVITY_SERVER = "/Applications/Antigravity.app/Contents/Resources/bin/language_server"
ANTIGRAVITY_QUOTA_RPC = "/exa.language_server_pb.LanguageServerService/RetrieveUserQuotaSummary"
ANTIGRAVITY_STALE = 180
ANTIGRAVITY_RESPONSE_MAX = 256 * 1024


def _antigravity_flag(argv: list[str], name: str) -> str | None:
    values = []
    for index, arg in enumerate(argv):
        if arg == name and index + 1 < len(argv):
            values.append(argv[index + 1])
        elif arg.startswith(name + "="):
            values.append(arg[len(name) + 1:])
    return values[0] if len(values) == 1 else None


def _existing_antigravity_endpoints() -> list[tuple[int, str]]:
    """PID-owned loopback targets and ephemeral CSRF, held only in memory.

    psutil/lsof merely inventory existing processes and sockets. No process
    command lines, raw lsof output, or CSRF values are logged or persisted.
    A missing app/dependency produces unavailable quota; there is no fallback
    that launches an app, an IDE language server, or an OAuth flow.
    """
    if sys.platform != "darwin" or psutil is None:
        return []
    endpoints = []
    try:
        # Filter executable/owner BEFORE reading arguments. Unrelated process
        # command lines can contain credentials and are never inspected here.
        processes = psutil.process_iter(["pid", "exe", "uids"])
        for proc in processes:
            try:
                info = proc.info
                uid = getattr(info.get("uids"), "real", None)
                if uid != os.getuid() or info.get("exe") != ANTIGRAVITY_SERVER:
                    continue
                argv = proc.cmdline()
                if (not isinstance(argv, list) or not argv
                        or argv[0] != ANTIGRAVITY_SERVER
                        or not all(isinstance(arg, str) for arg in argv)):
                    continue
                csrf = _antigravity_flag(argv, "--csrf_token")
                if not csrf or not re.fullmatch(r"[A-Za-z0-9_.\-]{8,256}", csrf):
                    continue
                pid = info.get("pid")
                if isinstance(pid, bool) or not isinstance(pid, int) or pid <= 0:
                    continue
                listing = subprocess.run(
                    ["/usr/sbin/lsof", "-a", "-p", str(pid), "-iTCP",
                     "-sTCP:LISTEN", "-nP", "-Fn"],
                    capture_output=True, text=True, timeout=2, check=False,
                )
                if listing.returncode != 0:
                    continue
                # Wildcard listeners owned by this exact app PID are also
                # reachable through loopback. Never use an external address.
                ports = sorted({int(match.group(1))
                                for line in listing.stdout.splitlines()
                                if (match := re.fullmatch(
                                    r"n(?:127\.0\.0\.1|\[::1\]|\*):(\d+)", line))
                                and 1 <= int(match.group(1)) <= 65535})
                endpoints.extend((port, csrf) for port in ports[:4])
                if len(endpoints) >= 4:
                    break
            except Exception:
                # Process exit/access races are ordinary. Exception text may
                # contain a command line, so deliberately do not print it.
                continue
    except Exception:
        return []
    return endpoints[:4]


def parse_antigravity_quota(data, now: float | None = None) -> dict | None:
    """Claude & GPT usage first; Gemini usage once Claude & GPT is truly empty.

    Three separate units exist (Claude & GPT usage, Gemini usage,
    AI credits). The cell reads the two usage pools in order and never credits.
    Empty = a valid, unexpired Claude & GPT bucket with remainingFraction == 0
    exactly; rounding never switches pools, so a non-empty pool shows <= 99%.
    Missing Claude & GPT data is No data, not a reason to show Gemini. Cycles
    and pools are never added or averaged. _a_reset_at is collector-only.
    """
    if not isinstance(data, dict) or not isinstance(data.get("response"), dict):
        return None
    groups = data["response"].get("groups")
    if not isinstance(groups, list):
        return None
    now = time.time() if now is None else now
    pools = {0: [], 1: []}          # 0 Gemini, 1 Claude & GPT
    for group in groups:
        if not isinstance(group, dict):
            continue
        group_name = group.get("displayName")
        if not isinstance(group_name, str):
            continue
        group_index = {"gemini models": 0, "claude and gpt models": 1}.get(
            group_name.strip().lower())
        buckets = group.get("buckets")
        if group_index is None or not isinstance(buckets, list):
            continue
        for bucket in buckets:
            if not isinstance(bucket, dict) or bucket.get("disabled", False) is not False:
                continue
            if "usageKnown" in bucket and bucket["usageKnown"] is not True:
                continue
            fraction = bucket.get("remainingFraction")
            if (isinstance(fraction, bool) or not isinstance(fraction, (int, float))
                    or not math.isfinite(fraction) or not 0 <= fraction <= 1):
                continue
            bucket_id = bucket.get("bucketId")
            label = bucket.get("displayName")
            if not isinstance(bucket_id, str) or not isinstance(label, str):
                continue
            prefix = "gemini-" if group_index == 0 else "3p-"
            if not bucket_id.startswith(prefix):
                continue
            id_cycle = {prefix + "5h": 0, prefix + "weekly": 1}.get(bucket_id)
            label_cycle = {"five hour limit remaining": 0,
                           "weekly limit remaining": 1}.get(label.strip().lower())
            if id_cycle is not None and label_cycle is not None and id_cycle != label_cycle:
                continue
            cycle = id_cycle if id_cycle is not None else label_cycle
            code = group_index * 2 + cycle if cycle is not None else 4 + group_index
            reset_at = None
            reset_time = bucket.get("resetTime")
            if isinstance(reset_time, str):
                try:
                    dt = datetime.fromisoformat(reset_time.replace("Z", "+00:00"))
                    if dt.tzinfo is not None:
                        reset_at = dt.timestamp()
                except (ValueError, OverflowError):
                    pass
            # At the advertised deadline this old window no longer describes
            # the current quota. Wait for another actual app reading.
            if reset_at is not None and reset_at <= now:
                continue
            pools[group_index].append((100.0 * (1.0 - fraction), code, reset_at, fraction == 0))

    def pick(candidates):
        # Most constrained bucket; deterministic ties; one bucket, no pseudo-quota.
        used, code, reset_at, empty = max(candidates, key=lambda c: (c[0], -c[1]))
        minutes = -1 if reset_at is None else max(0, int(round((reset_at - now) / 60)))
        return {"a": 100 if empty else min(99, int(round(used))), "ar": minutes,
                "ab": code, "_a_reset_at": reset_at}

    if not pools[1]:
        return None
    if not any(c[3] for c in pools[1]) or not pools[0]:
        return pick(pools[1])     # Claude & GPT left, or no Gemini reading → stay
    return pick(pools[0])


async def _poll_antigravity_app() -> dict | None:
    """Read ONLY a running, same-user official app's existing local endpoint.

    TLS verification is relaxed for this pinned loopback self-signed listener
    only. Proxies and redirects are disabled so CSRF cannot leave this Mac.
    forceRefresh:false reads the app's current account snapshot. It has no
    upstream generatedAt, so freshness means time of successful local read.
    """
    endpoints = await asyncio.to_thread(_existing_antigravity_endpoints)
    if not endpoints:
        log("AntiGravity app quota unavailable (no existing app listener)")
        return None
    try:
        async with httpx.AsyncClient(timeout=5.0, verify=False, trust_env=False,
                                     follow_redirects=False) as http:
            for port, csrf in endpoints:
                try:
                    resp = await http.post(
                        f"https://127.0.0.1:{port}{ANTIGRAVITY_QUOTA_RPC}",
                        headers={"Content-Type": "application/json",
                                 "Connect-Protocol-Version": "1",
                                 "X-Codeium-Csrf-Token": csrf},
                        json={"forceRefresh": False},
                    )
                    if resp.status_code != 200 or len(resp.content) > ANTIGRAVITY_RESPONSE_MAX:
                        continue
                    result = parse_antigravity_quota(resp.json())
                    if result is not None:
                        result["_a_sampled_at"] = time.monotonic()
                        return result
                except (ValueError, TypeError, httpx.HTTPError):
                    continue
    except httpx.HTTPError:
        pass
    log("AntiGravity app quota unavailable (local read failed)")
    return None


async def poll_antigravity() -> dict | None:
    """Read from the running app only; closed app means no data (tile shows --%)."""
    return await _poll_antigravity_app()


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def _extract_access_token(blob: str) -> str | None:
    """Pull the accessToken out of a credentials blob.

    Claude Code stores credentials as a JSON object; the blob may also be
    nested ({"claudeAiOauth": {"accessToken": "..."}}). Fall back to a
    regex match so unexpected shapes still work, and finally treat the
    blob as a raw token if nothing else matches.
    """
    blob = blob.strip()
    if not blob:
        return None
    try:
        data = json.loads(blob)
    except json.JSONDecodeError:
        data = None
    if isinstance(data, dict):
        # direct: {"accessToken": "..."}
        if isinstance(data.get("accessToken"), str):
            return data["accessToken"]
        # nested: {"claudeAiOauth": {"accessToken": "..."}}
        for v in data.values():
            if isinstance(v, dict) and isinstance(v.get("accessToken"), str):
                return v["accessToken"]
    m = re.search(r'"accessToken"\s*:\s*"([^"]+)"', blob)
    if m:
        return m.group(1)
    # Raw token (no JSON wrapper) — must look plausible (sk-ant-... etc.)
    if re.fullmatch(r"[A-Za-z0-9_\-.~+/=]{20,}", blob):
        return blob
    return None


def _read_token_keychain() -> str | None:
    try:
        out = subprocess.run(
            [
                "security",
                "find-generic-password",
                "-s",
                KEYCHAIN_SERVICE,
                "-a",
                getpass.getuser(),
                "-w",
            ],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except subprocess.CalledProcessError as e:
        log(f"Keychain read failed (rc={e.returncode}): {e.stderr.strip()}")
        return None
    except (FileNotFoundError, subprocess.TimeoutExpired) as e:
        log(f"Keychain access error: {e}")
        return None
    return _extract_access_token(out.stdout)


def _read_token_file() -> str | None:
    try:
        raw = CREDENTIALS_PATH.read_text()
    except OSError as e:
        log(f"Error reading credentials: {e}")
        return None
    return _extract_access_token(raw)


def read_token() -> str | None:
    if sys.platform == "darwin":
        return _read_token_keychain()
    return _read_token_file()


def load_cached_address() -> str | None:
    if not SAVED_ADDR_FILE.exists():
        return None
    addr = SAVED_ADDR_FILE.read_text().strip()
    # Accept both Linux MAC (AA:BB:CC:DD:EE:FF) and macOS CoreBluetooth UUID
    # (E621E1F8-C36C-495A-93FC-0C247A3E6E5F).
    if re.fullmatch(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", addr) or re.fullmatch(
        r"[0-9A-Fa-f]{8}-(?:[0-9A-Fa-f]{4}-){3}[0-9A-Fa-f]{12}", addr
    ):
        return addr
    log("Cached address malformed, discarding")
    SAVED_ADDR_FILE.unlink(missing_ok=True)
    return None


def save_address(addr: str) -> None:
    SAVED_ADDR_FILE.parent.mkdir(parents=True, exist_ok=True)
    SAVED_ADDR_FILE.write_text(addr)


async def scan_for_device() -> str | None:
    log(f"Scanning for '{DEVICE_NAME}' ({SCAN_TIMEOUT}s)...")
    # Match by NAME *or* by the custom data-service UUID carried in the scan
    # response. On macOS, CoreBluetooth frequently reports a *bonded* peripheral
    # (this board pairs as a HID keyboard) with local_name=None, so a name-only
    # match silently never fires even though the device is right there. The
    # 128-bit service UUID is unique to this firmware and always present in the
    # active-scan response, so it's the reliable discriminator.
    want_uuid = SERVICE_UUID.lower()
    found = await BleakScanner.discover(timeout=SCAN_TIMEOUT, return_adv=True)
    for addr, (dev, adv) in found.items():
        uuids = [u.lower() for u in (adv.service_uuids or [])]
        name = adv.local_name or dev.name
        if name == DEVICE_NAME or want_uuid in uuids:
            how = "name" if name == DEVICE_NAME else "service-uuid"
            log(f"Found: {addr} (matched by {how}, name={name!r})")
            return addr
    log(f"  ({len(found)} BLE devices seen, none matched)")
    return None


def _mins_until(iso_ts) -> int:
    """Minutes from now until an ISO-8601 `resets_at` (e.g. '2026-07-12T10:00:00+00:00')."""
    if not iso_ts:
        return -1
    try:
        dt = datetime.fromisoformat(iso_ts)
    except (TypeError, ValueError):
        return -1
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    mins = (dt - datetime.now(timezone.utc)).total_seconds() / 60.0
    return int(round(mins)) if mins > 0 else 0


async def poll_codex() -> dict | None:
    """Return the explicit 7-day Codex window, never a shorter substitute."""
    try:
        tok = json.loads(CODEX_AUTH_PATH.read_text())["tokens"]
        headers = {
            "Authorization": f"Bearer {tok['access_token']}",
            "chatgpt-account-id": tok["account_id"],
            "User-Agent": "codex_cli_rs/0.60.0",
        }
        async with httpx.AsyncClient(timeout=20.0) as http:
            resp = await http.get(CODEX_USAGE_URL, headers=headers)
        if resp.status_code >= 400:
            log(f"Codex usage HTTP {resp.status_code}")
            return None
        js = resp.json()
        rl = js.get("rate_limit") or {}
        windows = (rl.get("primary_window"), rl.get("secondary_window"))
        weekly = next((w for w in windows if isinstance(w, dict)
                       and w.get("limit_window_seconds") == 604800), None)
        if weekly is None:
            log("Codex usage has no weekly window")
            return None
        pct = weekly.get("used_percent")
        if (isinstance(pct, bool) or not isinstance(pct, (int, float))
                or not math.isfinite(pct) or not 0 <= pct <= 100):
            log("Codex weekly usage unavailable or invalid")
            return None
        secs = weekly.get("reset_after_seconds")
        if secs is None:
            reset_at = weekly.get("reset_at")
            if (not isinstance(reset_at, bool) and isinstance(reset_at, (int, float))
                    and math.isfinite(reset_at) and reset_at > 0):
                secs = reset_at - time.time()
        reset_mins = -1
        if (not isinstance(secs, bool) and isinstance(secs, (int, float))
                and math.isfinite(secs)):
            reset_mins = max(0, int(round(secs / 60)))

        result = {"c": pct, "cr": reset_mins}
        # Same authenticated response/account as the weekly window. Never use
        # rounded display percent to infer exhaustion (99.9 is still quota).
        if pct >= 100:
            credits = js.get("credits") or {}
            amount = credits.get("balance") if isinstance(credits, dict) else None
            try:
                balance = float(amount) if isinstance(amount, (int, float, str)) and not isinstance(amount, bool) else -1
                if not math.isfinite(balance) or balance < 0:
                    balance = -1
            except (ValueError, OverflowError):
                balance = -1
            result.update(cm=1, cc=balance, cs=0)
        return result
    except (OSError, KeyError, AttributeError, TypeError, ValueError, httpx.HTTPError) as e:
        log(f"Codex usage failed ({type(e).__name__})")
        return None


async def poll_claude(token: str | None) -> dict | None:
    if not token:
        log("Claude credentials unavailable")
        return None
    headers = dict(API_HEADERS_TEMPLATE)
    headers["Authorization"] = f"Bearer {token}"
    try:
        async with httpx.AsyncClient(timeout=20.0) as http:
            resp = await http.get(USAGE_URL, headers=headers)
    except httpx.HTTPError as e:
        log(f"Usage API failed ({type(e).__name__})")
        return None
    if resp.status_code >= 400:
        log(f"Usage API HTTP {resp.status_code}")
        return None
    try:
        data = resp.json()
    except ValueError:
        log("Usage API returned non-JSON")
        return None

    # Prefer the structured `limits[]` array — it carries the per-model
    # "weekly_scoped" entry (Fable 5) alongside session (5h) and weekly_all (7d).
    session_pct = weekly_pct = fable_pct = 0
    session_reset = weekly_reset = fable_reset = -1
    session_sev = "normal"

    for lim in (data.get("limits") or []):
        kind = lim.get("kind")
        pct = int(round(lim.get("percent") or 0))
        rst = _mins_until(lim.get("resets_at"))
        if kind == "session":
            session_pct, session_reset = pct, rst
            session_sev = lim.get("severity") or "normal"
        elif kind == "weekly_all":
            weekly_pct, weekly_reset = pct, rst
        elif kind == "weekly_scoped":
            model = (lim.get("scope") or {}).get("model") or {}
            if "fable" in (model.get("display_name") or "").lower():
                fable_pct, fable_reset = pct, rst

    # Fallback to the flat objects if `limits[]` is absent (older schema).
    if not data.get("limits"):
        fh = data.get("five_hour") or {}
        sd = data.get("seven_day") or {}
        session_pct = int(round(fh.get("utilization") or 0))
        session_reset = _mins_until(fh.get("resets_at"))
        weekly_pct = int(round(sd.get("utilization") or 0))
        weekly_reset = _mins_until(sd.get("resets_at"))

    payload = {
        "s": session_pct,
        "sr": session_reset,
        "w": weekly_pct,
        "wr": weekly_reset,
        "f": fable_pct,          # Fable 5 weekly-scoped utilization (0 if no such limit)
        "fr": fable_reset,       # minutes until the Fable limit resets (-1 if none)
        "st": "allowed" if session_sev == "normal" else "limited",
        "ok": True,
    }
    return payload


def unavailable_payload() -> dict:
    return {"s": -1, "sr": -1, "w": -1, "wr": -1, "f": -1, "fr": -1,
            "st": "unavailable", "ok": False, "a": -1, "ar": -1, "ab": -1}


async def poll_api(token: str | None) -> dict:
    """Poll each provider independently; a failed provider has no cached success."""
    results = await asyncio.gather(poll_claude(token), poll_codex(), poll_antigravity(),
                                   return_exceptions=True)
    payload = unavailable_payload()
    for name, result in zip(("Claude", "Codex", "AntiGravity"), results):
        if isinstance(result, Exception):
            log(f"{name} usage failed ({type(result).__name__})")
        elif result is not None:
            payload.update(result)
    return payload


# --- Mac system load (CPU / GPU / RAM / disk) -------------------------------
# None of these sources need sudo:
#   CPU  psutil.cpu_percent since the previous sample, across all cores
#   GPU  ioreg IOAccelerator "Device Utilization %"
#   RAM  (total - available) / total — the basis of psutil's own percent, so the
#        "used / total GB" caption always agrees with the ring
#   Disk the Data volume ("/" is only the read-only system snapshot) and the two
#        external drives; percent is used / (used + free), the same basis as the
#        "GB free" caption
# A reading that can't be taken is -1 (the display shows --%), never 0, and a
# sample older than SYSTEM_STALE is dropped rather than replayed.
SYSTEM_INTERVAL = 3          # seconds between system-load samples
SYSTEM_STALE = 15            # drop a sample this old (sampler thread stuck)
DATA_VOLUME = "/System/Volumes/Data" if os.path.isdir("/System/Volumes/Data") else "/"
# Percent key, free-GB key, mount point. An unplugged drive reads -1.
EXTERNAL_VOLUMES = (("sp", "sf", os.environ.get("CLAUDE_CTRL_DISK2", "")),
                    ("hp", "hf", os.environ.get("CLAUDE_CTRL_DISK3", "")))
EXTERNAL_INTERVAL = 30       # seconds between external drive reads
# cpu/gpu/ram/dsk: percent · ru/rt: RAM used/total GB · df: disk free GB ·
# nc/ng: CPU logical cores / GPU cores · sp/sf, hp/hf: external drives
SYSTEM_KEYS = ("cpu", "gpu", "ram", "dsk", "ru", "rt", "df", "nc", "ng",
               "sp", "sf", "hp", "hf")


def unavailable_system() -> dict:
    return {key: -1 for key in SYSTEM_KEYS}


def _pct(value) -> int:
    """Round a finite 0-100 reading to an int percent; anything else is -1."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return -1
    if not math.isfinite(value) or not 0 <= value <= 100:
        return -1
    return int(round(value))


# Absolute path: launchd's PATH (see the plist) has no /usr/sbin, so a bare
# "ioreg" is not found there and the GPU gauge would sit at --%.
IOREG_BIN = "/usr/sbin/ioreg"


def _read_ioreg_gpu() -> str:
    return subprocess.run([IOREG_BIN, "-r", "-d", "1", "-w", "0", "-c", "IOAccelerator"],
                          capture_output=True, text=True, timeout=2).stdout


def parse_gpu(ioreg_out: str) -> tuple[int, int]:
    """(utilization %, core count) from ioreg output; -1 for whatever is missing."""
    util = re.search(r'"Device Utilization %"\s*=\s*(\d+)', ioreg_out or "")
    cores = re.search(r'"gpu-core-count"\s*=\s*(\d+)', ioreg_out or "")
    return (_pct(int(util.group(1))) if util else -1,
            int(cores.group(1)) if cores else -1)


def read_external_volumes() -> dict:
    """Percent and free GB per drive. A mount point that isn't mounted is -1: the
    empty folder left behind would otherwise report the Mac's own disk."""
    stats = {}
    for pct_key, free_key, mount in EXTERNAL_VOLUMES:
        stats[pct_key] = stats[free_key] = -1
        try:
            if psutil is not None and os.path.ismount(mount):
                du = psutil.disk_usage(mount)
                if du.used + du.free > 0:
                    stats[pct_key] = _pct(du.used / (du.used + du.free) * 100)
                    stats[free_key] = int(du.free // 10**9)
        except OSError:
            pass
    return stats


class SystemSampler:
    """Samples this Mac's load on its own short cadence, independent of API polls."""

    def __init__(self) -> None:
        self._stats = unavailable_system()
        self._sampled_at = None
        self._warned: set[str] = set()   # log each failing source once, not every 3s
        self._external = {key: -1 for row in EXTERNAL_VOLUMES for key in row[:2]}
        self._external_thread = None
        self._external_at = None
        if psutil is not None:
            try:
                psutil.cpu_percent(interval=None)   # prime: next call measures from now
            except Exception:
                pass

    def _warn(self, source: str, err: Exception) -> None:
        if source not in self._warned:
            self._warned.add(source)
            log(f"System {source} unavailable ({type(err).__name__}: {err})")

    def _refresh_external(self) -> None:
        self._external = read_external_volumes()

    def _kick_external(self) -> None:
        # Own thread: a dying USB drive can hang statfs, and that must not freeze
        # the CPU/GPU/RAM readings along with it.
        now = time.monotonic()
        if self._external_at is not None and now - self._external_at < EXTERNAL_INTERVAL:
            return
        if self._external_thread is not None and self._external_thread.is_alive():
            self._external = {key: -1 for key in self._external}   # hung: can't vouch for the numbers
            return
        self._external_at = now
        self._external_thread = threading.Thread(target=self._refresh_external, daemon=True)
        self._external_thread.start()

    def sample_once(self) -> dict:
        stats = unavailable_system()
        stats.update(self._external)
        self._kick_external()
        if psutil is None:
            self._warn("psutil", ImportError("psutil not installed"))
        else:
            try:
                stats["cpu"] = _pct(psutil.cpu_percent(interval=None))
                stats["nc"] = psutil.cpu_count(logical=True) or -1
            except Exception as e:
                self._warn("CPU", e)
            try:
                vm = psutil.virtual_memory()
                if vm.total > 0:
                    used = vm.total - vm.available
                    stats["ram"] = _pct(used / vm.total * 100)
                    if stats["ram"] >= 0:
                        stats["ru"] = round(used / 2**30, 1)
                        stats["rt"] = int(round(vm.total / 2**30))
            except Exception as e:
                self._warn("RAM", e)
            try:
                du = psutil.disk_usage(DATA_VOLUME)
                if du.used + du.free > 0:
                    stats["dsk"] = _pct(du.used / (du.used + du.free) * 100)
                    if stats["dsk"] >= 0:
                        stats["df"] = int(du.free // 10**9)
            except Exception as e:
                self._warn("disk", e)
        try:
            stats["gpu"], stats["ng"] = parse_gpu(_read_ioreg_gpu())
        except Exception as e:
            self._warn("GPU", e)
        return stats

    def latest(self) -> dict:
        if self._sampled_at is None or time.monotonic() - self._sampled_at > SYSTEM_STALE:
            return unavailable_system()
        return dict(self._stats)

    async def run(self, stop_event: asyncio.Event) -> None:
        while not stop_event.is_set():
            self._stats = await asyncio.to_thread(self.sample_once)
            self._sampled_at = time.monotonic()
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=SYSTEM_INTERVAL)
            except asyncio.TimeoutError:
                pass


# --- 3D printer (through Moonraker) ----------------------------
# The ring dashboard's bottom row shows the running print: percent, time left
# and whether it is really printing. Read-only GETs on the LAN, nothing is ever
# sent to the printer. CLAUDE_CTRL_PRINTER="" turns this off: the payload then
# has no printer keys and the board keeps its spinner line.
#
# /printer/info is read first and wins over print_stats: when Klipper shut down
# mid-print print_stats still said "paused", which
# would have read as a harmless pause. A reading that can't be taken is offline
# (ps 0) with every other key -1 — never a replay of an older sample.
PRINTER_URL = os.environ.get("CLAUDE_CTRL_PRINTER", "").rstrip("/")
PRINTER_INTERVAL = 10        # seconds between printer samples
PRINTER_STALE = 60           # a sample this old reads as offline
PRINTER_STALL = 600          # "printing" but the file position still for this long → stalled
PRINTER_TIMEOUT = 5.0
# ps codes, shared with the firmware's PS_* enum in ui.cpp (a test keeps them equal).
# PS_HALTED: paused BY THE PRINTER — print_stats.exception carries its reason
# (filament runout, noodle detected…). A pause you pressed has no exception and
# stays PS_PAUSED.
(PS_OFFLINE, PS_IDLE, PS_PRINTING, PS_PAUSED, PS_DONE,
 PS_SHUTDOWN, PS_FAULT, PS_STALLED, PS_ERROR, PS_HALTED) = range(10)
# ps: state · pp: file progress % · pm: minutes left · pl/pL: layer / total ·
# pq: minutes without progress (stalled only) · pr: the printer's reason (halted/error)
PRINTER_KEYS = ("ps", "pp", "pm", "pl", "pL", "pq", "pr")
_PRINTER_CAP = 99999         # keeps a garbage reading from blowing the serial line length
PRINTER_REASON_MAX = 20      # fits the row's detail line (sans_16, 176px)
# A job that stopped mid-print: the row turns red and macOS gets one notification.
PRINTER_ALERT = (PS_HALTED, PS_STALLED, PS_SHUTDOWN, PS_ERROR)


def unavailable_printer() -> dict:
    return {"ps": PS_OFFLINE, "pp": -1, "pm": -1, "pl": -1, "pL": -1, "pq": -1, "pr": ""}


def _reason(stats: dict) -> str:
    """The printer's own reason for stopping a job (print_stats.exception.message,
    e.g. "e2_filament runout"), as plain ASCII — the board's fonts have nothing
    else — capped to the detail line. "" when there is none."""
    exc = stats.get("exception")
    msg = exc.get("message") if isinstance(exc, dict) else None
    if not isinstance(msg, str):
        return ""
    msg = re.sub(r"[^ -~]+", " ", msg.replace("_", " "))
    return " ".join(msg.split())[:PRINTER_REASON_MAX].rstrip()


def _count(value) -> int:
    """A non-negative int (layer, minutes) from a reading; -1 if it isn't one."""
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        return -1
    return min(int(value), _PRINTER_CAP) if value >= 0 else -1


def classify_printer(klippy: str | None, status: dict | None, stalled_s: float = 0.0) -> dict:
    """Payload keys from one reading: klippy is /printer/info's state (None =
    unreachable), status the objects query (None = not readable)."""
    out = unavailable_printer()
    if klippy is None:
        return out
    stats = (status or {}).get("print_stats") or {}
    state = stats.get("state")
    job = state in ("printing", "paused")

    if klippy != "ready":
        # A job that was running when Klipper went down is a stopped print;
        # without one it is a printer fault (or just booting).
        if job:
            out["ps"] = PS_SHUTDOWN
        else:
            out["ps"] = PS_FAULT if klippy in ("shutdown", "error") else PS_OFFLINE
    elif state == "printing":
        out["ps"] = PS_STALLED if stalled_s >= PRINTER_STALL else PS_PRINTING
        if out["ps"] == PS_STALLED:
            out["pq"] = _count(stalled_s // 60)
    elif state == "paused":
        out["ps"] = PS_HALTED if _reason(stats) else PS_PAUSED
    elif state == "complete":
        out["ps"] = PS_DONE
    elif state == "error":
        out["ps"] = PS_ERROR
    else:                        # standby, cancelled
        out["ps"] = PS_IDLE

    if out["ps"] in (PS_HALTED, PS_ERROR):
        out["pr"] = _reason(stats)
    # Progress only for a job on the bed; a finished or idle printer shows none.
    if out["ps"] in (PS_PRINTING, PS_PAUSED, PS_STALLED, PS_SHUTDOWN, PS_ERROR, PS_HALTED):
        progress = ((status or {}).get("display_status") or {}).get("progress")
        pct = _pct(progress * 100) if isinstance(progress, (int, float)) and not isinstance(progress, bool) else -1
        out["pp"] = pct
        duration = stats.get("print_duration")
        # Usual estimate: elapsed / progress − elapsed. Under 1% the ratio is
        # noise, so no estimate.
        if pct >= 0 and progress >= 0.01 and _count(duration) >= 0:
            out["pm"] = _count(round(duration * (1 / progress - 1) / 60))
        info = stats.get("info") or {}
        out["pl"] = _count(info.get("current_layer"))
        out["pL"] = _count(info.get("total_layer"))
    return out


def notify(title: str, body: str) -> None:
    """A macOS Notification Center banner (with a sound). The texts go in as
    osascript arguments, never spliced into the script, so quotes in a printer
    message can't break or inject into it."""
    log(f"Notify: {title} — {body}")
    try:
        subprocess.run(["/usr/bin/osascript",
                        "-e", "on run argv",
                        "-e", 'display notification (item 2 of argv) with title (item 1 of argv) sound name "Basso"',
                        "-e", "end run", title, body],
                       capture_output=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired) as e:
        log(f"Notification failed ({type(e).__name__})")


_ALERT_WHAT = {PS_HALTED: "Máy in tự dừng", PS_STALLED: "Máy in đứng yên",
               PS_SHUTDOWN: "Klipper tắt giữa chừng", PS_ERROR: "Bản in hỏng"}


def alert_text(s: dict) -> str:
    parts = [_ALERT_WHAT[s["ps"]]]
    if s["pp"] >= 0:
        parts.append(f"ở {s['pp']}%")
    if s["pl"] >= 0 and s["pL"] > 0:
        parts.append(f"(lớp {s['pl']}/{s['pL']})")
    text = " ".join(parts)
    if s["ps"] == PS_STALLED and s["pq"] >= 0:
        text += f" — {s['pq']} phút không chạy"
    if s.get("pr"):
        text += f" — {s['pr']}"
    return text


class PrinterSampler:
    """Polls the printer on its own cadence; tracks whether the job is moving
    and raises one notification per stopped-mid-print incident."""

    def __init__(self, url: str = PRINTER_URL) -> None:
        self.url = url
        self._stats = unavailable_printer()
        self._sampled_at = None
        self._file_pos = None
        self._was_printing = False
        self._moved_at = time.monotonic()
        self._last_ps = None
        self._alerted = False    # this incident has been notified

    async def _read(self, http) -> tuple[str | None, dict | None]:
        try:
            resp = await http.get(self.url + "/printer/info")
            resp.raise_for_status()
            klippy = resp.json()["result"]["state"]
        except (httpx.HTTPError, ValueError, KeyError, TypeError):
            return None, None
        try:
            # Bare object names = every field. Built by hand: an empty query
            # value ("print_stats=") is not the same request to Moonraker.
            resp = await http.get(self.url + "/printer/objects/query"
                                  "?print_stats&display_status&virtual_sdcard")
            resp.raise_for_status()
            status = resp.json()["result"]["status"]
        except (httpx.HTTPError, ValueError, KeyError, TypeError):
            status = None
        return klippy, status

    async def sample_once(self, http) -> dict | None:
        """One reading's payload keys, or None when the reading is incomplete:
        Klipper answered "ready" but the objects query failed. Such blips
        (several a night on some printers) used to flash the row to "not printing"
        for 10 s; now the last sample stands until PRINTER_STALE says offline."""
        klippy, status = await self._read(http)
        if klippy == "ready" and status is None:
            return None
        now = time.monotonic()
        state = ((status or {}).get("print_stats") or {}).get("state")
        pos = ((status or {}).get("virtual_sdcard") or {}).get("file_position")
        printing = klippy == "ready" and state == "printing"
        # The stall clock runs only across consecutive printing samples: a
        # resume after a long pause starts it fresh. No position to compare is
        # no evidence of a stall, so that counts as moving too.
        if not (printing and self._was_printing) or pos is None or pos != self._file_pos:
            self._moved_at = now
        self._file_pos = pos
        self._was_printing = printing
        stats = classify_printer(klippy, status, now - self._moved_at if printing else 0.0)
        if stats["ps"] != self._last_ps:
            self._last_ps = stats["ps"]
            log(f"Printer: klippy={klippy} print_stats={state} → " + json.dumps(stats, separators=(",", ":")))
        # One notification per incident. Going offline (Wi-Fi blip) keeps the
        # incident open; only the job running again or ending closes it.
        if stats["ps"] in PRINTER_ALERT:
            if not self._alerted:
                self._alerted = True
                await asyncio.to_thread(notify, "Máy in 3D dừng giữa chừng", alert_text(stats))
        elif stats["ps"] != PS_OFFLINE:
            self._alerted = False
        return stats

    def latest(self) -> dict:
        if self._sampled_at is None or time.monotonic() - self._sampled_at > PRINTER_STALE:
            return unavailable_printer()
        return dict(self._stats)

    async def run(self, stop_event: asyncio.Event) -> None:
        async with httpx.AsyncClient(timeout=PRINTER_TIMEOUT) as http:
            while not stop_event.is_set():
                stats = await self.sample_once(http)
                if stats is not None:
                    self._stats = stats
                    self._sampled_at = time.monotonic()
                try:
                    await asyncio.wait_for(stop_event.wait(), timeout=PRINTER_INTERVAL)
                except asyncio.TimeoutError:
                    pass


class UsageCollector:
    """One API polling schedule shared by BLE and USB, including failed polls.

    With a SystemSampler attached, every snapshot also carries the latest
    machine load, and with a PrinterSampler the printer row; without them
    (tests, older callers) the payload is unchanged.
    """

    def __init__(self, system: SystemSampler | None = None,
                 printer: PrinterSampler | None = None) -> None:
        self._payload = unavailable_payload()
        self._sampled_at = time.monotonic()
        self._antigravity_reset_at = None
        self._antigravity_sampled_at = self._sampled_at
        self._codex_sampled_at = self._sampled_at
        self.revision = 0
        self.system = system
        self.printer = printer

    async def poll_once(self) -> None:
        token = await asyncio.to_thread(read_token)
        self._payload = await poll_api(token)
        self._antigravity_reset_at = self._payload.pop("_a_reset_at", None)
        self._sampled_at = time.monotonic()
        self._codex_sampled_at = self._sampled_at
        self._antigravity_sampled_at = self._payload.pop("_a_sampled_at", self._sampled_at)
        self.revision += 1
        log("Usage sample: " + json.dumps(self._payload, separators=(",", ":")))

    async def poll_codex_once(self) -> None:
        result = await poll_codex()
        for key in ("c", "cr", "cm", "cc", "cs"):
            self._payload.pop(key, None)
        self._payload.update(result or {"c": -1, "cr": -1})
        self._codex_sampled_at = time.monotonic()
        self.revision += 1

    def snapshot(self, with_system: bool = True) -> dict:
        """Latest payload with fresh countdowns and clock.

        with_system=False leaves out the machine-load and printer keys: BLE
        sends only on a new API sample (every 90 s), so those numbers would be
        stale, and the extra keys would push the write past what macOS allows
        in one BLE write-without-response (BLE_PAYLOAD_MAX).
        """
        payload = dict(self._payload)
        elapsed = max(0.0, time.monotonic() - self._sampled_at)
        for key in ("sr", "wr", "fr", "cr"):
            if payload.get(key, -1) >= 0:
                payload[key] = max(0, int(round((payload[key] * 60 - elapsed) / 60)))
        codex_age = max(0.0, time.monotonic() - self._codex_sampled_at)
        if payload.get("cr", -1) >= 0:
            payload["cr"] = max(0, int(round((self._payload["cr"] * 60 - codex_age) / 60)))
        if codex_age > CODEX_STALE:
            if payload.get("cm") == 1:
                payload["cs"] = 1  # keep last known amount, visibly old
            else:
                payload.update(c=-1, cr=-1)
        antigravity_age = max(0.0, time.monotonic() - self._antigravity_sampled_at)
        if (antigravity_age > ANTIGRAVITY_STALE or
                (self._antigravity_reset_at is not None and
                 time.time() >= self._antigravity_reset_at)):
            payload.update({"a": -1, "ar": -1, "ab": -1})
        elif self._antigravity_reset_at is not None:
            payload["ar"] = max(0, int(round((self._antigravity_reset_at - time.time()) / 60)))
        lt = time.localtime()
        payload["t"] = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec
        if with_system and self.system is not None:
            payload.update(self.system.latest())
        if with_system and self.printer is not None:
            payload.update(self.printer.latest())
        return payload

    async def run(self, stop_event: asyncio.Event) -> None:
        # One collector owns all provider reads; Codex updates every 30s while
        # Claude/AntiGravity retain their existing 90s schedule. No overlap.
        next_full = next_codex = 0.0
        while not stop_event.is_set():
            started = time.monotonic()
            if started >= next_full:
                await self.poll_once()
                next_full = started + POLL_INTERVAL
            else:
                await self.poll_codex_once()
            next_codex = started + CODEX_POLL_INTERVAL
            delay = max(0.1, min(next_full, next_codex) - time.monotonic())
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=delay)
            except asyncio.TimeoutError:
                pass


class Session:
    def __init__(self, client: BleakClient) -> None:
        self.client = client
        self.refresh_requested = asyncio.Event()

    def _on_refresh(self, _char, _data: bytearray) -> None:
        log("Refresh requested by device")
        self.refresh_requested.set()

    async def setup_refresh_subscription(self) -> None:
        try:
            await self.client.start_notify(REQ_CHAR_UUID, self._on_refresh)
        except (BleakError, ValueError) as e:
            log(f"Refresh subscription unavailable: {e}")

    async def write_payload(self, payload: dict) -> bool:
        data = json.dumps(payload, separators=(",", ":")).encode()
        try:
            await self.client.write_gatt_char(RX_CHAR_UUID, data, response=False)
            return True
        except BleakError as e:
            log(f"Write failed: {e}")
            return False


async def connect_and_run(address: str, stop_event: asyncio.Event,
                          collector: UsageCollector) -> bool:
    """Connect and send shared samples until disconnected or stopped.

    Returns True if the connection was used successfully (so the caller
    keeps the cached address), False if the connection failed and the
    cache should be invalidated.
    """
    log(f"Connecting to {address}...")
    client = BleakClient(address)
    try:
        await client.connect()
    except (BleakError, asyncio.TimeoutError) as e:
        log(f"Connection failed: {e}")
        return False

    if not client.is_connected:
        log("Connection failed (no error but not connected)")
        return False

    log("Connected")
    session = Session(client)
    await session.setup_refresh_subscription()

    last_revision = -1
    used_successfully = False
    try:
        while client.is_connected and not stop_event.is_set():
            if session.refresh_requested.is_set() or collector.revision != last_revision:
                session.refresh_requested.clear()
                revision = collector.revision
                if await session.write_payload(collector.snapshot(with_system=False)):
                    last_revision = revision
                    used_successfully = True

            try:
                await asyncio.wait_for(session.refresh_requested.wait(), timeout=TICK)
            except asyncio.TimeoutError:
                pass
    finally:
        try:
            await client.disconnect()
        except BleakError:
            pass

    log("Device disconnected" if not stop_event.is_set() else "Stopping")
    return used_successfully


def _detect_board_serial():
    """Blocking: find and open the display's serial port via the `whoami`
    handshake. Only usbserial (UART-bridge) ports are probed — a native-USB
    device on usbmodem is left alone. Returns an open serial.Serial, or None."""
    if not _HAVE_SERIAL:
        return None
    for dev in sorted(glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.wchusbserial*")
                     + glob.glob("/dev/cu.SLAB_USBtoUART*") + glob.glob("/dev/ttyUSB*")):
        try:
            s = pyserial.Serial(dev, SERIAL_BAUD, timeout=1)
        except (OSError, pyserial.SerialException):
            continue
        try:
            time.sleep(2.5)              # opening resets the ESP32 — let it boot
            s.reset_input_buffer()
            s.write(b"whoami\n")
            s.flush()
            deadline = time.time() + 4
            while time.time() < deadline:
                line = s.readline().decode("utf-8", "replace").strip()
                if line.startswith(SERIAL_WHOAMI_TOKEN):
                    return s             # matched — keep the port open
        except (OSError, pyserial.SerialException):
            pass
        try:
            s.close()
        except Exception:
            pass
    return None


async def serial_loop(stop_event: asyncio.Event, collector: UsageCollector) -> None:
    """Stream the shared snapshot without blocking the USB lease on API calls."""
    if not _HAVE_SERIAL:
        log("pyserial not installed — USB serial path disabled (BLE only)")
        return
    ser = None
    wrote_once = False   # log the first successful serial write, then stay quiet
    try:
        while not stop_event.is_set():
            if ser is None:
                ser = await asyncio.to_thread(_detect_board_serial)
                if ser is None:
                    try:
                        await asyncio.wait_for(stop_event.wait(), timeout=SERIAL_RESCAN_DELAY)
                    except asyncio.TimeoutError:
                        pass
                    continue
                log(f"USB serial: display on {ser.port} — streaming over USB (priority over BLE)")

            data = (json.dumps(collector.snapshot(), separators=(",", ":")) + "\n").encode()
            try:
                await asyncio.to_thread(ser.write, data)
                await asyncio.to_thread(ser.flush)
                if not wrote_once:
                    log("USB usage stream active")
                    wrote_once = True
            except (OSError, pyserial.SerialException) as e:
                log(f"USB serial write failed ({e}); re-detecting the display")
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                continue

            try:
                await asyncio.wait_for(stop_event.wait(), timeout=SERIAL_WRITE_INTERVAL)
            except asyncio.TimeoutError:
                pass
    finally:
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass


async def ble_main_loop(stop_event: asyncio.Event, collector: UsageCollector) -> None:
    backoff = SCAN_RETRY_MIN
    while not stop_event.is_set():
        address = load_cached_address()
        if not address:
            address = await scan_for_device()
            if address:
                save_address(address)
                backoff = SCAN_RETRY_MIN
            else:
                log(f"Device not found, retrying in {backoff}s...")
                try:
                    await asyncio.wait_for(stop_event.wait(), timeout=backoff)
                except asyncio.TimeoutError:
                    pass
                backoff = min(backoff * 2, SCAN_RETRY_MAX)
                continue

        ok = await connect_and_run(address, stop_event, collector)
        if not ok:
            log("Invalidating cached address")
            SAVED_ADDR_FILE.unlink(missing_ok=True)
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=backoff)
            except asyncio.TimeoutError:
                pass
            backoff = min(backoff * 2, SCAN_RETRY_MAX)
        else:
            # Link was working and then dropped: reconnect by cached address
            # (no scan), but pause briefly so we don't reconnect in a tight,
            # radio-hogging burst that would stutter a nearby BLE mouse.
            backoff = SCAN_RETRY_MIN
            try:
                await asyncio.wait_for(stop_event.wait(), timeout=RECONNECT_DELAY)
            except asyncio.TimeoutError:
                pass


async def main() -> None:
    stop_event = asyncio.Event()
    loop = asyncio.get_running_loop()

    def _stop(*_args: object) -> None:
        log("Daemon stopping")
        stop_event.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, _stop)
        except NotImplementedError:
            signal.signal(sig, _stop)

    log("=== Claude Usage Tracker Daemon (BLE + USB, macOS) ===")
    log(f"Poll interval: {POLL_INTERVAL}s  |  USB serial path: {'on' if _HAVE_SERIAL else 'off (no pyserial)'}"
        f"  |  system load every {SYSTEM_INTERVAL}s{'' if psutil else ' (no psutil: CPU/RAM/disk off)'}")

    system = SystemSampler()
    printer = PrinterSampler() if PRINTER_URL else None
    log(f"Printer row: {PRINTER_URL} every {PRINTER_INTERVAL}s" if printer
        else "Printer row off (CLAUDE_CTRL_PRINTER empty)")
    collector = UsageCollector(system, printer)
    tasks = [system.run(stop_event), collector.run(stop_event),
             serial_loop(stop_event, collector)]
    if printer is not None:
        tasks.append(printer.run(stop_event))
    if BLE_ENABLED:
        tasks.append(ble_main_loop(stop_event, collector))
    else:
        log("BLE off (CLAUDE_CTRL_BLE=0) — USB serial only")
    await asyncio.gather(*tasks)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(0)
