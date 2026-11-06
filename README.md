# Clawdmeter CYD (copy)

Màn hình để bàn, cắm USB vào Mac, hiện mức dùng Claude, Codex, AntiGravity, tải CPU, GPU, RAM, dung lượng ổ đĩa và máy in 3D.

## Cần có

- Mac (GPU đọc bằng `ioreg` của macOS).
- Board **ESP32-3248S035R**: màn 3.5" ST7796, cảm ứng điện trở XPT2046. Bản `S035C` cảm ứng điện dung sẽ không chạm được.
- Cáp USB có truyền dữ liệu.
- [Homebrew](https://brew.sh), Python 3.10 trở lên (`brew install python`).
- Đã đăng nhập Claude Code trên máy này (`claude`, rồi `/login`).

## Cài trong 2 lệnh

Cắm board vào Mac, mở Terminal trong thư mục này:

```bash
./flash-cyd.sh
```

Lần đầu mất khoảng 5–10 phút để tải thư viện. Nếu máy có nhiều thiết bị USB: `./flash-cyd.sh /dev/cu.usbserial-XXXX`.

```bash
./install-mac.sh
```

Sau khoảng 10 giây, board có số thật. Chương trình nền tự chạy mỗi lần đăng nhập Mac.

Đặt board nằm ngang, cổng USB bên trái. Chạm nửa trái hoặc nửa phải màn để chuyển màn hình (màn Settings dùng mũi tên).

Cài xong có thể xoá thư mục này, chương trình nền đã được chép sang `~/Library/Application Support/clawdmeter/`. Giữ lại nếu còn muốn đổi tên ổ hoặc flash lại.

## Tuỳ chọn

Các tuỳ chọn này truyền khi chạy `install-mac.sh`. Chạy lại lúc nào cũng được, nhưng mỗi lần phải truyền đủ các tuỳ chọn muốn giữ:

```bash
DISK2=/Volumes/TenO1 DISK3=/Volumes/TenO2 PRINTER=http://192.168.1.50:7125 ./install-mac.sh
```

- `DISK2`, `DISK3`: hai hàng phụ của ô Disk. Không đặt thì hiện `Not plugged in`.
- `PRINTER`: địa chỉ Moonraker của máy in 3D. Không đặt thì ô máy in không có dữ liệu.
- `BLE=1`: gửi thêm qua Bluetooth. macOS sẽ hỏi quyền lần đầu.

Muốn đổi tên hàng ổ đĩa: sửa `DISK_NAMES` trong `firmware/src/ui.cpp`, dừng chương trình nền (lệnh ở cuối trang), chạy lại `./flash-cyd.sh`, rồi bật lại.

## Số liệu lấy từ đâu

Mọi thứ chạy trên máy bạn, không gửi về nơi nào khác ngoài nhà cung cấp tương ứng.

- **Claude:** token Claude Code trong Keychain, hỏi `api.anthropic.com`.
- **Codex:** `~/.codex/auth.json`, hỏi `chatgpt.com`. Không dùng Codex thì ô hiện `--%`.
- **AntiGravity:** chỉ đọc khi app đang mở. Đóng app thì ô hiện `--%`.
- **CPU, GPU, RAM, Disk:** đọc ngay trên Mac.

Mất dữ liệu quá 30 giây (Mac ngủ, chương trình nền dừng): CPU, GPU, RAM, Disk hiện `--%` và `No link`; ba ô quota giữ số cuối và ghi `Old data`.

## Khi có lỗi

- **Không thấy cổng USB:** đổi cáp, hoặc cài [driver CP2102](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers).
- **Màn sáng nhưng không có số:** xem log bên dưới. Chương trình nền giữ cổng USB, nên phải gỡ nó trước khi flash lại.
- **Không có số dù đã cắm:** kiểm tra tên cổng bằng `ls /dev/cu.*`. Chương trình nền chỉ tìm `cu.usbserial*`, `cu.wchusbserial*`, `cu.SLAB_USBtoUART*`.
- **Chạm lệch:** làm theo mục Hiệu chuẩn cảm ứng.

```bash
tail -F ~/Library/Logs/claude-usage-daemon.out.log
```

Dừng chương trình nền (cần khi flash lại):

```bash
launchctl unload ~/Library/LaunchAgents/com.user.claude-usage-daemon.plist
```

Bật lại:

```bash
launchctl load -w ~/Library/LaunchAgents/com.user.claude-usage-daemon.plist
```

## Hiệu chuẩn cảm ứng

Mỗi tấm cảm ứng lệch một ít, số có sẵn được đo trên một board khác. Nếu chạm lệch, dừng chương trình nền rồi chạy:

```bash
PLATFORMIO_WORKSPACE_DIR=$HOME/.cache/pio-ws/clawdmeter-cyd pio run -d firmware -e cyd_diag -t upload -t monitor
```

Chạm vào 4 mũi tên đỏ ở 4 góc. Terminal in ra dòng `CALDATA: a, b, c, d, e`. Bấm Ctrl+C, chép 5 số đó vào `touch_cal` trong `firmware/src/boards/cyd_28/display.cpp`, rồi chạy lại `./flash-cyd.sh` và bật lại chương trình nền.

## Nguồn và bản quyền

Dựa trên [HermannBjorgvin/Clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter). Hình động Clawd lấy từ [claudepix](https://claudepix.vercel.app) của [@amaanbuilds](https://x.com/amaanbuilds), icon từ [Lucide](https://lucide.dev).

Font: [Inter](https://github.com/rsms/inter) và [Source Serif 4](https://github.com/adobe-fonts/source-serif) theo giấy phép SIL Open Font License (xem `firmware/fonts/`), DejaVu Sans Mono theo giấy phép DejaVu. Giấy phép của icon Lucide cũng nằm ở thư mục đó. Chúng thay cho font thương mại của bản gốc.

Hình Clawd không thuộc quyền của dự án, và repo này chưa có giấy phép mở. Chỉ dùng cá nhân, đừng bán.
