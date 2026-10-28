![](logo.png)

# Remmina UltraViewer Protocol Plugin (`remmina-plugin-ultraviewer`)

[English](README.md) • [Tiếng Việt](README.vi.md)

[![License: GPL v2+](https://img.shields.io/badge/License-GPLv2%2B-blue.svg)](LICENSE)
[![Remmina: 1.4+](https://img.shields.io/badge/Remmina-%E2%89%A51.4.0-orange.svg)](https://remmina.org/)
[![Version: 0.2.0](https://img.shields.io/badge/Version-0.2.0-brightgreen.svg)]()

A native, high-performance Remmina protocol plugin providing full remote desktop client connectivity to [UltraViewer](https://www.ultraviewer.net/) hosts on Linux.

Developed by reverse-engineering the UltraViewer wire protocol, this plugin integrates seamlessly into Remmina's user interface, connection profiles, Quick Connect toolbar, and tool features.

---

## Key Features

- **High-Performance Screen Streaming**:
  - Zstandard dictionary-decompressed screen updates with custom pre-trained dictionary (`8bit.dat`).
  - High-quality JPEG tile atlas decompression via `libjpeg-turbo`.
  - RGB555 bottom-up color space transformation and Cairo rendering.
  - Aspect-ratio-preserving fit (`MIN(sx, sy)`) with solid black letterbox/pillarbox margins—no distortion on mismatched displays (e.g., 3:2 Surface Pro on 16:9 laptop displays).
  - Dynamic display resolution re-initialization via the **Refresh Screen** tool action.

- **Audio Streaming**:
  - Remote computer sound playback via ITU-T G.722 sub-band ADPCM audio decoding at 16,000 Hz.
  - Low-latency PulseAudio output stream with dynamic mute/unmute toggle.

- **Remote Hardware Cursor**:
  - Full Windows DIB cursor decoding supporting 1, 4, 8, 16, 24, and 32 bpp formats.
  - Dual XOR and AND transparency mask compositing with inverted pixel support.
  - Dynamic on-the-fly cursor shape cache requests (Command 4) and position tracking (Command 28).
  - Runtime toggleable remote cursor visibility ("Show Remote Cursor" preference).

- **Input & Keyboard Combinations**:
  - Precise mouse coordinate mapping accounting for scale and centering offsets.
  - Remote Event modifier key emulation (`Ctrl`, `Alt`, `Shift`) allowing native combinations like `Ctrl+A`, `Ctrl+C`, `Ctrl+V`.
  - Windows Administrative Actions: Send `Ctrl+Alt+Delete` (CAD), Lock Computer, and Remote Reboot.

- **Clipboard & File Transfer**:
  - Bi-directional UTF-8/UTF-16 text clipboard synchronization with echo suppression (Command 33 and legacy Command 5).
  - Native file transfer manager supporting arbitrary file sizes with 64 KB chunking (Commands 13–21).

- **Integrated Chat**:
  - Native in-session text chat utilizing Remmina's built-in chat window (`remmina_chat_window`).

- **Remmina Quick Connect & Interactive Auth**:
  - **Quick Connect Toolbar Integration**: Dynamically injects `ULTRAVIEWER` into Remmina's main window Quick Connect dropdown, persisting across main window closes and reopenings from the system tray.
  - **Interactive Password Prompt**: Directly prompts for the partner password on the connection screen if not pre-saved in the profile.
  - **In-Place Auth Retry**: If an incorrect password is entered, displays an auth retry panel allowing the user to re-enter credentials without terminating or restarting the connection.

---

## Architecture & Technical Documentation

Comprehensive protocol analysis and implementation research are available in [`docs/`](docs/):

- [**UltraViewer Protocol Specification**](docs/ultraviewer-protocol.md): Wire-level reference for the UltraViewer remote desktop protocol:
  - Transport & TLS SNI requirements, HTTP tunneling preamble.
  - 3DES command channel cipher (`DES-EDE-CBC`) and registration/lookup formats.
  - Relay envelopes (System `0x01` and Forward `0x00`).
  - Screen streaming pipeline (Command 0 `ScreenInit`, Command 1 tiles, Zstandard dictionary, JPEG atlas, RGB555).
  - Remote hardware cursor protocol (Commands 3, 4, 28) and DIB decoding.
  - Keyboard & mouse event encoding (binary Command 2).
  - In-session chat (Command 12) and typing notifications (Commands 29, 30).
  - Audio streaming (Command 46 activation, Command 43 G.722 payload).
  - File transfer state machine (Commands 13–21).
  - Screen refresh and renegotiation mechanics.

- [**Remmina Protocol Plugin Architecture**](docs/remmina-plugin-research.md): Technical architecture and Remmina integration guide:
  - Plugin ABI and `RemminaProtocolPlugin` descriptor table.
  - Lifecycle management (`init`, `open_connection`, `close_connection`).
  - Multi-threaded rendering with GTK `DrawingArea`, Cairo surface lifecycle, and mutex protection.
  - Remmina feature tools (`REMMINA_PROTOCOL_FEATURE_TYPE_TOOL` and `PREF`).
  - Interactive authentication panel (`protocol_plugin_init_auth`).
  - Dynamic Quick Connect injection mechanism.

---

## Dependencies & Requirements

### Build Dependencies

- **Remmina** $\ge$ 1.4.0 (including headers in `/usr/include/remmina`)
- **GTK+ 3** (`gtk+-3.0` $\ge$ 3.22)
- **OpenSSL** (`libssl`, `libcrypto` $\ge$ 1.1.1 or 3.x)
- **libzstd** ($\ge$ 1.4.0)
- **libjpeg-turbo** (`libjpeg`)
- **libpulse** (`libpulse-simple`)
- **CMake** $\ge$ 3.16
- **pkg-config** / **pkgconf**
- **GCC** or **Clang** (C11 support)

### Arch Linux Dependencies

```bash
sudo pacman -S remmina gtk3 openssl zstd libjpeg-turbo libpulse cmake pkgconf
```

---

## Proprietary Asset (`8bit.dat`)

UltraViewer screen decoding relies on a pre-trained Zstandard dictionary (`8bit.dat`, 112,640 bytes, SHA-256 `9aa45789d9ff9d329a623a685d57b67e33c777019de9ce1f273f21860e4bfe6b`). Because this is a proprietary asset from the official UltraViewer distribution, it is not checked into git directly.

To extract `8bit.dat` automatically from the official installer:

```bash
./scripts/extract_assets.sh assets
```

The script will:
1. Check for existing copies in local paths or checkouts.
2. If missing, download `UltraViewer_setup_6.6.133_vi.exe` from official CDN mirrors.
3. Extract `8bit.dat` using `innoextract`.
4. Validate the cryptographic SHA-256 checksum.

---

## Building and Installation

> [!WARNING]
> Remember to fetch the proprietary assets before building !

```bash
cd remmina-plugin-ultraviewer
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
sudo cmake --install .
```

To run the unit test suite:

```bash
ctest -V
```

---

## Credit

Special thanks to [DeanOpen's MacViewer](https://github.com/DeanOpen/MacViewer) project. Their code & documents are very helpful resources that made this project possible.

---

## License & Disclaimers

This project is licensed under the **GNU General Public License v2.0 or later** ([GPL-2.0-or-later](LICENSE)), with the following notices and exceptions:

- **Proprietary Asset Exception**: The pre-shared dictionary file `8bit.dat` (in `assets/`) is extracted from the official UltraViewer installer and is the proprietary property of **DucFabulous Co., Ltd.** It is not covered by or licensed under the GNU General Public License.
- **Trademarks**: "UltraViewer" is a registered trademark and product of **DucFabulous Co., Ltd.**
- **Non-Endorsement Disclaimer**: This plugin is an independent open-source project developed through reverse-engineering the UltraViewer network protocol. This project is not affiliated with, sponsored by, or endorsed by **DucFabulous Co., Ltd.**
