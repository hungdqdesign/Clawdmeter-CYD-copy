#!/bin/bash
# macOS installer for Clawdmeter daemon (Python + bleak + launchd).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVICE_LABEL="com.user.claude-usage-daemon"
# BLE device name this daemon binds to. Default matches the firmware.
CTRL_NAME="${CTRL_NAME:-Claude Controller}"
# USB only by default: the board is powered from this Mac anyway, and a Bluetooth
# scan without macOS permission kills the process in a restart loop.
#   BLE=1 ./install-mac.sh     # also send over Bluetooth
BLE="${BLE:-0}"
# Optional extras, all empty = off:
#   PRINTER=http://<printer-ip>:7125      Moonraker address for the 3D printer cell
#   DISK2=/Volumes/<name>  DISK3=/Volumes/<name>   the two extra rows of the Disk cell
PRINTER="${PRINTER:-}"
DISK2="${DISK2:-}"
DISK3="${DISK3:-}"
PLIST_SRC="$SCRIPT_DIR/daemon/$SERVICE_LABEL.plist"
PLIST_DST="$HOME/Library/LaunchAgents/$SERVICE_LABEL.plist"
# Venv and script are copied out of this folder: launchd can be denied access to
# Downloads, Desktop and Documents, and the folder may be moved or deleted later.
APP_DIR="$HOME/Library/Application Support/clawdmeter"
VENV_DIR="${VENV_DIR:-$APP_DIR/venv}"
DAEMON_PY="$APP_DIR/claude_usage_daemon.py"
LOG_DIR="$HOME/Library/Logs"
LOG_OUT="$LOG_DIR/claude-usage-daemon.out.log"
LOG_ERR="$LOG_DIR/claude-usage-daemon.err.log"

echo "=== Clawdmeter macOS install ==="
echo ""

echo "[1/5] Checking prerequisites..."
for cmd in python3 curl; do
    command -v "$cmd" >/dev/null || { echo "Error: $cmd is required"; exit 1; }
done
# The daemon's syntax needs 3.10+; the python3 that ships with macOS is 3.9.
python3 -c 'import sys; sys.exit(sys.version_info < (3, 10))' || {
    echo "Error: Python 3.10+ is required (found $(python3 --version 2>&1))."
    echo "  Install it with:  brew install python"
    exit 1
}
if ! security find-generic-password -s "Claude Code-credentials" -a "$USER" -w >/dev/null 2>&1; then
    echo "Warning: Claude Code OAuth token not found in Keychain (service 'Claude Code-credentials')."
    echo "  Sign in via Claude Code first, then re-run this installer."
    echo "  Continuing anyway — the daemon will retry on each poll."
fi
echo "  OK"
echo ""

echo "[2/5] Creating Python virtualenv..."
if [ ! -d "$VENV_DIR" ]; then
    python3 -m venv "$VENV_DIR"
fi
"$VENV_DIR/bin/pip" install --quiet --upgrade pip
# pyserial powers the USB-priority data path (stream usage over the wired USB
# link, which the firmware prioritises over BLE). Optional — the daemon falls
# back to BLE-only if it's missing. psutil feeds the CPU/RAM/disk gauges
# (optional too — without it those gauges read --%).
"$VENV_DIR/bin/pip" install --quiet "bleak>=0.22" "httpx>=0.27" "pyserial>=3.5" "psutil>=5.9"
PYTHON_BIN="$VENV_DIR/bin/python"
echo "  OK ($PYTHON_BIN)"
echo ""

echo "[3/5] Rendering launchd plist..."
mkdir -p "$HOME/Library/LaunchAgents" "$LOG_DIR" "$APP_DIR"
cp "$SCRIPT_DIR/daemon/claude_usage_daemon.py" "$DAEMON_PY"
# Escape for the plist's XML, then for the sed replacement below.
esc() { printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/[&|\\]/\\&/g'; }
sed \
    -e "s|__PYTHON_BIN__|$(esc "$PYTHON_BIN")|g" \
    -e "s|__DAEMON_PATH__|$(esc "$DAEMON_PY")|g" \
    -e "s|__REPO_DIR__|$(esc "$APP_DIR")|g" \
    -e "s|__LOG_OUT__|$(esc "$LOG_OUT")|g" \
    -e "s|__LOG_ERR__|$(esc "$LOG_ERR")|g" \
    -e "s|__HOME__|$(esc "$HOME")|g" \
    -e "s|__CTRL_NAME__|$(esc "$CTRL_NAME")|g" \
    -e "s|__BLE__|$(esc "$BLE")|g" \
    -e "s|__PRINTER__|$(esc "$PRINTER")|g" \
    -e "s|__DISK2__|$(esc "$DISK2")|g" \
    -e "s|__DISK3__|$(esc "$DISK3")|g" \
    "$PLIST_SRC" > "$PLIST_DST"
if [ "$BLE" = "0" ]; then
    echo "  Installed: $PLIST_DST  (USB only — BLE off)"
else
    echo "  Installed: $PLIST_DST  (binds to BLE name: '${CTRL_NAME}')"
fi
echo ""

if [ "$BLE" = "0" ]; then
    echo "[4/5] Bluetooth permission check... skipped (BLE=0)"
else
echo "[4/5] Bluetooth permission check..."
echo "  On first run the daemon will trigger a Bluetooth permission prompt."
echo "  macOS only prompts for foreground processes — so we'll run it"
echo "  interactively once below. Press Ctrl+C after you see 'Scanning...'"
echo "  and grant permission when prompted. Then re-run this installer"
echo "  (or just continue) to enable launchd autostart."
echo ""
read -r -p "Run a permission-priming scan now? [Y/n] " ans
if [[ ! "$ans" =~ ^[Nn]$ ]]; then
    CLAUDE_CTRL_NAME="$CTRL_NAME" "$PYTHON_BIN" "$DAEMON_PY" || true
fi
fi
echo ""

echo "[5/5] Loading launchd service..."
launchctl unload "$PLIST_DST" 2>/dev/null || true
launchctl load -w "$PLIST_DST"
echo "  Loaded."
echo ""

echo "=== Done ==="
echo ""
echo "Useful commands:"
echo "  launchctl list | grep claude-usage     # check it's running"
echo "  tail -F $LOG_OUT                       # live logs"
echo "  launchctl unload $PLIST_DST            # stop"
echo "  launchctl load -w $PLIST_DST           # start"
