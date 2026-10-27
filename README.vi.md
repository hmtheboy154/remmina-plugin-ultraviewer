![](logo.png)

# Plugin Giao thức UltraViewer cho Remmina (`remmina-plugin-ultraviewer`)

[English](README.md) • [Tiếng Việt](README.vi.md)

[![License: GPL v2+](https://img.shields.io/badge/License-GPLv2%2B-blue.svg)](LICENSE)
[![Remmina: 1.4+](https://img.shields.io/badge/Remmina-%E2%89%A51.4.0-orange.svg)](https://remmina.org/)
[![Version: 0.2.0](https://img.shields.io/badge/Version-0.2.0-brightgreen.svg)]()

Plugin giao thức Remmina native hiệu năng cao, cung cấp khả năng kết nối điều khiển máy tính từ xa tới các máy chủ [UltraViewer](https://www.ultraviewer.net/) trên Linux.

Được phát triển thông qua kỹ thuật dịch ngược (reverse-engineering) giao thức mạng của UltraViewer, plugin này tích hợp liền mạch vào giao diện người dùng của Remmina, các hồ sơ kết nối (connection profile), thanh công cụ Quick Connect và các công cụ điều khiển trong phiên làm việc.

---

## Tính năng chính

- **Truyền phát hình ảnh màn hình hiệu năng cao**:
  - Giải nén luồng cập nhật màn hình bằng Zstandard với từ điển huấn luyện sẵn (`8bit.dat`).
  - Giải nén tile atlas định dạng JPEG chất lượng cao thông qua `libjpeg-turbo`.
  - Chuyển đổi không gian màu RGB555 bottom-up và kết xuất trực tiếp bằng Cairo.
  - Tự động co giãn bảo toàn tỷ lệ khung hình (`MIN(sx, sy)`) với viền đen (letterbox/pillarbox)—không bị méo hình khi tỷ lệ màn hình khác nhau (ví dụ: màn hình 3:2 của Surface Pro trên màn hình laptop 16:9).
  - Tái khởi tạo độ phân giải màn hình động thông qua công cụ **Refresh Screen**.

- **Truyền phát âm thanh (Audio Streaming)**:
  - Phát âm thanh máy tính từ xa thông qua bộ giải mã ITU-T G.722 sub-band ADPCM ở tần số 16.000 Hz.
  - Luồng phát âm thanh độ trễ thấp qua PulseAudio với nút bật/tắt (mute/unmute) tức thì.

- **Con trỏ chuột phần cứng từ xa (Remote Hardware Cursor)**:
  - Giải mã đầy đủ định dạng con trỏ Windows DIB hỗ trợ 1, 4, 8, 16, 24 và 32 bpp.
  - Xử lý mặt nạ trong suốt XOR và AND kép, hỗ trợ các điểm ảnh đảo màu (inverted pixel).
  - Tự động yêu cầu nạp hình dạng con trỏ (Command 4) theo thời gian thực và theo dõi vị trí chuột (Command 28).
  - Tuỳ chọn bật/tắt hiển thị con trỏ máy từ xa linh hoạt ("Show Remote Cursor").

- **Nhập liệu & Tổ hợp phím**:
  - Ánh xạ toạ độ chuột chính xác, tự động bù trừ tỷ lệ co giãn và khoảng cách căn giữa lề.
  - Mô phỏng phím điều khiển từ xa (`Ctrl`, `Alt`, `Shift`) cho phép thực thi mượt mà các tổ hợp phím như `Ctrl+A`, `Ctrl+C`, `Ctrl+V`.
  - Các thao tác quản trị hệ thống Windows: Gửi `Ctrl+Alt+Delete` (CAD), Khoá máy tính (`Win+L`) và Khởi động lại máy từ xa.

- **Đồng bộ Clipboard & Truyền file**:
  - Đồng bộ clipboard văn bản UTF-8/UTF-16 hai chiều với cơ chế chống dội (echo suppression, hỗ trợ Command 33 và Command 5 cũ).
  - Trình quản lý truyền file native hỗ trợ kích thước file không giới hạn với cơ chế phân mảnh 64 KB (Command 13–21).

- **Trò chuyện trực tiếp (Integrated Chat)**:
  - Chat văn bản hai chiều ngay trong phiên điều khiển, tận dụng cửa sổ chat tích hợp của Remmina (`remmina_chat_window`).

- **Quick Connect & Xác thực tương tác trên Remmina**:
  - **Tích hợp thanh công cụ Quick Connect**: Tự động đưa giao thức `ULTRAVIEWER` vào danh sách Quick Connect trên cửa sổ chính Remmina, duy trì ngay cả khi đóng cửa sổ và mở lại từ khay hệ thống (System Tray).
  - **Hỏi mật khẩu tương tác**: Tự động hiển thị hộp thoại nhập mật khẩu đối tác trên màn hình kết nối nếu chưa lưu trước trong profile.
  - **Thử lại mật khẩu tại chỗ (In-Place Auth Retry)**: Khi nhập sai mật khẩu, Remmina sẽ yêu cầu nhập lại trực tiếp trên giao diện kết nối (tối đa 3 lần) mà không cần ngắt kết nối hay mở lại cửa sổ.

---

## Kiến trúc & Tài liệu kỹ thuật

Phân tích giao thức chuyên sâu và kiến trúc lập trình được lưu trữ chi tiết trong thư mục [`docs/`](docs/):

- [**Đặc tả Giao thức UltraViewer**](docs/ultraviewer-protocol.md): Tài liệu kỹ thuật mức byte-wire của giao thức UltraViewer:
  - Yêu cầu TLS SNI và HTTP tunnel preamble.
  - Mã hoá kênh điều khiển 3DES (`DES-EDE-CBC`), định dạng gói tin đăng ký (Command 3) và tìm kiếm đối tác (Command 8).
  - Định dạng Relay Envelope (System `0x01` và Forward `0x00`).
  - Luồng truyền dữ liệu màn hình (Command 0 `ScreenInit`, Command 1, từ điển Zstandard, JPEG atlas, RGB555).
  - Giao thức con trỏ từ xa (Command 3, 4, 28) và giải mã DIB/CUR.
  - Mã hoá sự kiện chuột và bàn phím (Binary Command 2).
  - Trò chuyện trong phiên (Command 12) và thông báo đang nhập (Command 29, 30).
  - Truyền phát âm thanh (Command 46 kích hoạt, Command 43 gói tin G.722, Command 42 yên lặng).
  - Quản lý truyền file (Command 13–21).
  - Cơ chế đàm phán lại khi làm mới màn hình và đổi độ phân giải.

- [**Kiến trúc Plugin Giao thức Remmina**](docs/remmina-plugin-research.md): Hướng dẫn phát triển plugin giao thức native cho Remmina:
  - ABI plugin và bảng mô tả `RemminaProtocolPlugin`.
  - Vòng đời kết nối (`init`, `open_connection`, `close_connection`).
  - Kết xuất đa luồng với GTK `DrawingArea`, vòng đời Cairo surface và cơ chế bảo vệ mutex.
  - Các công cụ và tính năng mở rộng (`REMMINA_PROTOCOL_FEATURE_TYPE_TOOL` và `PREF`).
  - Tích hợp bảng xác thực tương tác (`protocol_plugin_init_auth`).
  - Kỹ thuật inject động vào Quick Connect và xử lý vòng đời cửa sổ khay hệ thống.

---

## Thư viện phụ thuộc & Yêu cầu cài đặt

### Yêu cầu khi Build

- **Remmina** $\ge$ 1.4.0 (bao gồm header phát triển tại `/usr/include/remmina`)
- **GTK+ 3** (`gtk+-3.0` $\ge$ 3.22)
- **OpenSSL** (`libssl`, `libcrypto` $\ge$ 1.1.1 hoặc 3.x)
- **libzstd** ($\ge$ 1.4.0)
- **libjpeg-turbo** (`libjpeg`)
- **libpulse** (`libpulse-simple`)
- **CMake** $\ge$ 3.16
- **pkg-config** / **pkgconf**
- Trình biên dịch **GCC** hoặc **Clang** (hỗ trợ C11)

### Cài đặt thư viện trên Arch Linux

```bash
sudo pacman -S remmina gtk3 openssl zstd libjpeg-turbo libpulse cmake pkgconf
```

---

## Tệp dữ liệu độc quyền (`8bit.dat`)

Cơ chế giải nén màn hình của UltraViewer yêu cầu một từ điển nén Zstandard được huấn luyện sẵn (`8bit.dat`, kích thước 112.640 bytes, SHA-256 `9aa45789d9ff9d329a623a685d57b67e33c777019de9ce1f273f21860e4bfe6b`). Vì đây là tài nguyên độc quyền đi kèm bộ cài chính thức của UltraViewer, tệp này không được đưa trực tiếp vào kho mã nguồn git.

Để tự động tải và trích xuất `8bit.dat` từ bộ cài chính thức:

```bash
./scripts/extract_assets.sh assets
```

Script sẽ:
1. Kiểm tra bản sao sẵn có trên máy hoặc các kho mã nguồn lân cận.
2. Nếu chưa có, tải bộ cài `UltraViewer_setup_6.6.133_vi.exe` từ máy chủ chính thức.
3. Trích xuất `8bit.dat` bằng `7z` hoặc `innoextract`.
4. Kiểm tra mã băm SHA-256 đảm bảo tính toàn vẹn.

---

## Biên dịch và Cài đặt

> [!WARNING]
> Hãy nhớ lấy tệp dữ liệu độc quyền về trước khi tiến hành biên dịch!

```bash
cd remmina-plugin-ultraviewer
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
sudo cmake --install .
```

Chạy bộ kiểm thử đơn vị (unit tests):

```bash
ctest -V
```

---

## Công trạng

Xin gửi lời cảm ơn đặc biệt đến dự án [MacViewer của DeanOpen](https://github.com/DeanOpen/MacViewer). Mã nguồn và tài liệu từ dự án này là những nguồn tài nguyên vô cùng hữu ích, góp phần quan trọng giúp dự án này có thể thực hiện được.

---

## Giấy phép (License)

Dự án này được cấp phép theo các điều khoản của **GNU General Public License v2.0 hoặc mới hơn** ([GPL-2.0-or-later](LICENSE)).
