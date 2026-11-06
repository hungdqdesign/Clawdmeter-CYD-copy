#!/bin/bash
# Flash firmware Clawdmeter lên CYD ESP32-3248S035 (3.5") trên macOS.
# Cắm board vào USB trước khi chạy script này.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# Build workspace (.pio) OUTSIDE the repo and at a path WITHOUT spaces:
#  esptool 5.x (elf2image / upload) does not quote paths, so a workspace under
#  a folder with spaces in its name fails with "Path ... does not exist".
export PLATFORMIO_WORKSPACE_DIR="${PLATFORMIO_WORKSPACE_DIR:-$HOME/.cache/pio-ws/clawdmeter-cyd}"
BOARD="cyd_28"
PORT="$1"

# Tự động tìm cổng USB nếu không truyền tham số
if [ -z "$PORT" ]; then
    PORT=$(ls /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -1)
    if [ -z "$PORT" ]; then
        echo "❌  Không tìm thấy cổng USB. Cắm board CYD vào máy rồi thử lại."
        echo "    Tìm thủ công: ls /dev/cu.*"
        exit 1
    fi
fi

# Kiểm tra PlatformIO
if ! command -v pio >/dev/null; then
    if ! command -v brew >/dev/null; then
        echo "❌  Cần Homebrew để cài PlatformIO. Cài từ https://brew.sh rồi chạy lại."
        exit 1
    fi
    echo "⚠️  PlatformIO chưa được cài. Đang cài..."
    brew install platformio
    if ! command -v pio >/dev/null; then
        echo "❌  Đã cài PlatformIO nhưng chưa thấy lệnh pio. Mở Terminal mới rồi chạy lại."
        exit 1
    fi
fi

echo "======================================="
echo "  Flashing Clawdmeter → CYD 3.5\""
echo "  Board: $BOARD"
echo "  Port:  $PORT"
echo "======================================="
echo ""

cd "$SCRIPT_DIR/firmware"
pio run -e "$BOARD" -t upload --upload-port "$PORT"

echo ""
echo "✅  Flash xong! Board sẽ tự khởi động lại."
echo ""
echo "Xem log serial:  pio device monitor -p $PORT -b 115200"
echo ""
echo "Bước tiếp theo:  cd \"$SCRIPT_DIR\" && ./install-mac.sh"
