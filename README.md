# Remmina UltraViewer Protocol Plugin (`remmina-plugin-ultraviewer`)

A native Remmina protocol plugin providing remote desktop client connectivity to [UltraViewer](https://www.ultraviewer.net/) hosts.

## Documentation

Comprehensive research and engineering documentation is available in the [`docs/`](docs/) directory:

- [**UltraViewer Protocol Specification**](docs/ultraviewer-protocol.md): Wire-level reference for the UltraViewer remote desktop protocol, including transport & TLS SNI requirements, 3DES command channel cipher, relay envelopes, screen streaming (Zstandard + dictionary, JPEG atlas, RGB555), user input (mouse, binary keyboard), remote hardware cursor (.CUR parsing), text clipboard synchronization (Command 33/5), in-session chat (Command 12), file transfer (Commands 13–21), computer audio streaming (G.722, Commands 42/43), and remote administration tools (CAD, Lock, Reboot).
- [**Remmina Protocol Plugin Architecture**](docs/remmina-plugin-research.md): Architectural guide and design blueprint for building native Remmina protocol plugins, detailing `RemminaProtocolPlugin` descriptors, `RemminaPluginService` ABI, GTK DrawingArea / Cairo rendering pipeline, GDK input mapping, clipboard integration, chat dialog integration (`remmina_chat_window`), remote tools menu, and out-of-tree CMake & PKGBUILD packaging.

## Requirements

### Runtime & Build Dependencies

- **Remmina** $\ge$ 1.4.0 (with development headers `/usr/include/remmina`)
- **GTK+ 3** (`gtk+-3.0`)
- **OpenSSL** (`libssl`, `libcrypto`)
- **libzstd** (Zstandard compression library)
- **libjpeg-turbo** (JPEG atlas decompression)
- **CMake** $\ge$ 3.16
- **pkg-config** / **pkgconf**

### Proprietary Assets (`8bit.dat`)

UltraViewer screen decoding relies on a pre-trained Zstandard dictionary (`8bit.dat`, 112,640 bytes, SHA-256 `9aa45789d9ff9d329a623a685d57b67e33c777019de9ce1f273f21860e4bfe6b`). Because this is a proprietary UltraViewer asset, it is not distributed directly in this repository.

To download and extract `8bit.dat` automatically from the official installer:

```bash
./scripts/extract_assets.sh
```

The script will:
1. Verify existing local assets or borrow from a local MacViewer checkout.
2. Otherwise, download the official installer (`UltraViewer_setup_6.6.133_vi.exe`) and extract `8bit.dat` via `7z` or `innoextract`.
3. Verify the cryptographic SHA-256 digest.

## License

This project is licensed under the GNU General Public License v2.0 or later (GPL-2.0-or-later) - see the [LICENSE](LICENSE) file for details.
