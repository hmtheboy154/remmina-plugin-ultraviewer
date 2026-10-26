# UltraViewer Protocol Specification and Implementation Reference

This document provides a comprehensive, wire-level reference of the UltraViewer remote desktop protocol. It is derived from in-depth reverse engineering of the official UltraViewer protocol implementations in the MacViewer research tree (`Sources/UltraViewerProtocol` and `LinuxViewer/src`), cross-verified against live protocol captures and Windows client binaries.

This reference serves as the specification for building the native `remmina-plugin-ultraviewer` client (viewer/controller mode). Hosting an UltraViewer session on Linux is treated as out of scope for the client plugin, though protocol structures relevant to bidirectional negotiation are fully documented.

---

## Table of Contents

1. [Transport & Network Architecture](#1-transport--network-architecture)
   - [1.1 Network Topology](#11-network-topology)
   - [1.2 TLS Handshake & SNI Requirements](#12-tls-handshake--sni-requirements)
   - [1.3 HTTP Tunnel Preamble](#13-http-tunnel-preamble)
   - [1.4 Framing & Wire Conventions](#14-framing--wire-conventions)
2. [Relay Envelopes](#2-relay-envelopes)
   - [2.1 System Envelope (0x01)](#21-system-envelope-0x01)
   - [2.2 Forward Envelope (0x00)](#22-forward-envelope-0x00)
3. [Command Channel](#3-command-channel)
   - [3.1 Legacy 3DES Cipher (Server Cipher)](#31-legacy-3des-cipher-server-cipher)
   - [3.2 Registration (Command 3)](#32-registration-command-3)
   - [3.3 Partner Lookup (Command 8)](#33-partner-lookup-command-8)
   - [3.4 Command Keepalive](#34-command-keepalive)
4. [Relay Session Authentication](#4-relay-session-authentication)
   - [4.1 Partner Login Message](#41-partner-login-message)
   - [4.2 Acceptance Handshake](#42-acceptance-handshake)
5. [Session Lifecycle & Command Dispatch](#5-session-lifecycle--command-dispatch)
   - [5.1 Complete Command Matrix](#51-complete-command-matrix)
   - [5.2 Re-Login & Relay Switching](#52-re-login--relay-switching)
6. [Screen Initialization (Command 0)](#6-screen-initialization-command-0)
7. [Screen Frame Updates (Command 1)](#7-screen-frame-updates-command-1)
8. [Framebuffer & Rendering Pipeline](#8-framebuffer--rendering-pipeline)
   - [8.1 Buffer Layouts](#81-buffer-layouts)
   - [8.2 Frame Decode Steps](#82-frame-decode-steps)
   - [8.3 RGB555 Color Space Conversion](#83-rgb555-color-space-conversion)
   - [8.4 JPEG Atlas Tile Extraction](#84-jpeg-atlas-tile-extraction)
9. [Zstandard Stream & Dictionary Specification](#9-zstandard-stream--dictionary-specification)
10. [Remote Cursor Protocol](#10-remote-cursor-protocol)
    - [10.1 Cursor Position Updates (Command 28)](#101-cursor-position-updates-command-28)
    - [10.2 Cursor Shape Selection (Command 3)](#102-cursor-shape-selection-command-3)
    - [10.3 Cursor Shape Transfer (Command 4)](#103-cursor-shape-transfer-command-4)
    - [10.4 Windows .CUR File Structure](#104-windows-cur-file-structure)
11. [User Input Encoding](#11-user-input-encoding)
    - [11.1 Mouse Events](#111-mouse-events)
    - [11.2 Keyboard Events (Binary Mode)](#112-keyboard-events-binary-mode)
    - [11.3 Legacy Host Keyboard Limitations](#113-legacy-host-keyboard-limitations)
12. [Text Clipboard Synchronization](#12-text-clipboard-synchronization)
    - [12.1 Modern Compressed Mode (Command 33)](#121-modern-compressed-mode-command-33)
    - [12.2 Legacy Fallback (Command 5)](#122-legacy-fallback-command-5)
    - [12.3 Echo Suppression & Guard Windows](#123-echo-suppression--guard-windows)
13. [In-Session Chat Protocol](#13-in-session-chat-protocol)
    - [13.1 Message Format (Command 12)](#131-message-format-command-12)
    - [13.2 Typing Indicator (Commands 29 & 30)](#132-typing-indicator-commands-29--30)
14. [File Transfer Protocol](#14-file-transfer-protocol)
    - [14.1 Transfer Sequence & Chunking](#141-transfer-sequence--chunking)
    - [14.2 Wire Messages (Commands 13, 14, 15, 16, 21)](#142-wire-messages-commands-13-14-15-16-21)
15. [Computer Audio Streaming](#15-computer-audio-streaming)
    - [15.1 Audio Activation (Command 46)](#151-audio-activation-command-46)
    - [15.2 G.722 Audio Stream (Command 43) & Silence (Command 42)](#152-g722-audio-stream-command-43--silence-command-42)
16. [Remote Control Actions & Session Tools](#16-remote-control-actions--session-tools)
    - [16.1 System Actions (CAD, Reboot, Lock)](#161-system-actions-cad-reboot-lock)
    - [16.2 Quality & Display Settings (Commands 31 & 32)](#162-quality--display-settings-commands-31--32)
    - [16.3 Desktop Wallpaper Toggle (Command 23)](#163-desktop-wallpaper-toggle-command-23)
    - [16.4 Multi-Monitor Desktop Layout](#164-multi-monitor-desktop-layout)
    - [16.5 Reverse Control / Side Switch (Command 54)](#165-reverse-control--side-switch-command-54)
17. [Direct LAN P2P Mode](#17-direct-lan-p2p-mode)
18. [Asset Management & Setup Installer Extraction](#18-asset-management--setup-installer-extraction)
19. [Heartbeat, Keepalives & Timeout Budgets](#19-heartbeat-keepalives--timeout-budgets)
20. [Security Model, Resource Limits & Error Codes](#20-security-model-resource-limits--error-codes)
21. [Test Vectors & Synthetic Payloads](#21-test-vectors--synthetic-payloads)
22. [MacViewer / LinuxViewer Source Code Map](#22-macviewer--linuxviewer-source-code-map)

---

## 1. Transport & Network Architecture

### 1.1 Network Topology

An UltraViewer viewer session maintains two concurrent TLS connections:

```
[ Remmina Client ]
       │
       ├────── (TLS:443 SNI="ultraviewer") ─────► functions3.ultraviewer.net (Command Channel)
       │       • Register identity / local MAC
       │       • Lookup partner ID & session credentials
       │       • Heartbeats & async state
       │
       └────── (TLS:443 SNI="ultraviewer") ─────► relay-*.ultraviewer.net (Relay Channel)
               • HTTP preamble tunnel
               • Forward envelopes (Screen, Input, Clipboard, Chat, Files, Audio)
               • End-to-end partner session
```

1. **Command Channel**: TLS connection to `functions3.ultraviewer.net:443`.
   - Used to register the local client instance (Command `3`), obtain an UltraViewer ID and server credentials.
   - Used to look up the remote partner ID (Command `8`), obtaining the partner's public IP/port, designated relay server hostname/IP, and a per-session secret token.
   - Kept open throughout the session for 60-second keepalives.
2. **Relay Channel**: TLS connection to the relay server specified in the lookup response (`relay-*.ultraviewer.net:443`, resolved to an IPv4 address).
   - Serves as the transparent byte-pipe forwarder between viewer and host.
   - The relay routes packets based on destination IP and port headers embedded inside each forward envelope.

### 1.2 TLS Handshake & SNI Requirements

- **Strict SNI Requirement**: During TLS ClientHello, **the Server Name Indication (SNI) MUST be set to the literal ASCII string `"ultraviewer"`**, NOT the target domain name (`functions3.ultraviewer.net` or `relay-*.ultraviewer.net`). The relay routing infrastructure inspects this SNI label.
- **Hostname Verification**: Certificate validation must evaluate the actual endpoint hostname:
  - Command channel: `functions3.ultraviewer.net`.
  - Relay channel: the `relay_hostname` provided in lookup field 14 (falling back to `functions3.ultraviewer.net`).
- **Expired Certificate Handling**: UltraViewer relay servers occasionally serve an expired leaf certificate that Windows clients accept. If initial verification fails with `X509_V_ERR_CERT_HAS_EXPIRED`, the client should retry verification with the verification time set to `notAfter - 1`.
- **Intermediate CA Anchors**: Relay servers often omit the intermediate Sectigo CA certificate in TLS handshakes. Clients should bundle or trust:
  - *Sectigo Public Server Authentication Root R46* (SHA-256: `7BB647A62AEEAC88BF257AA522D01FFEA395E0AB45C73F93F65654EC38F25A06`)
  - *Sectigo Public Server Authentication CA DV R36* (SHA-256: `8C54C334B66BA4E426772AF4A3F9136C19A1AEC729FDB28C535C07A5A4EF22E0`)

### 1.3 HTTP Tunnel Preamble

Immediately following TLS handshake completion, both channels send a pseudo-HTTP POST request to establish the binary tunnel:

```http
POST /UVService/ HTTP/1.1\r\n
User-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\r\n
Content-Type: application/octet-stream\r\n
Connection: Keep-Alive\r\n
Content-length: 8589934590\r\n
Host: <actual_hostname>\r\n
\r\n
```

- The oversized `Content-length: 8589934590` triggers HTTP streaming mode on the relay.
- The server responds with an HTTP response header (e.g., `HTTP/1.1 200 OK...`). The client discards all bytes up to and including the delimiter `\r\n\r\n`.
- All subsequent bytes on the connection belong to binary length-prefixed protocol frames.

### 1.4 Framing & Wire Conventions

- **Length-Prefixed Frames**:
  ```text
  [ uint32_le payload_length ] [ uint8_t payload[payload_length] ]
  ```
  - Maximum standard frame length: 16 MiB (`16,777,216` bytes).
  - A zero-length frame (`0x00000000`) is valid keepalive padding and is discarded.
- **Multipart Payloads**:
  Peer application messages frequently bundle multiple binary buffers into a single frame:
  ```text
  repeat until frame exhausted:
      [ uint32_le part_length ]
      [ uint8_t   part_bytes[part_length] ]
  ```
  - Part count is implicit (continues until frame payload ends).
  - Maximum part count limit: 4,096 parts per frame. Maximum aggregate multipart size: 64 MiB.
- **Byte Order**: All integers (lengths, coordinates, flags, timestamps) are little-endian unless explicitly documented otherwise.
- **String Delimiters**: Text control messages use `:` or `|||` as field delimiters. Empty fields are preserved.

---

## 2. Relay Envelopes

Every frame transmitted over the relay channel begins with an envelope header identifying its purpose and destination.

### 2.1 System Envelope (`0x01`)

Used for command-channel communications, relay ping/pong, and keepalive pulses:

```text
[ 0x01 ] [ optional 0x00 ] [ payload ]
```

- **Relay ping**: `0x01` + ASCII `"ping"` (`01 70 69 6E 67`). Sent without the `0x00` flag.
- **Relay pong**: `0x01` + ASCII `"pong"` (`01 70 6F 6E 67`). Received from relay and ignored.
- **Command Keepalive**: `0x01 0x00` + 3DES-encrypted `"2"`.
- **Command Server Replies**: `0x01` + plaintext ASCII response text.

### 2.2 Forward Envelope (`0x00`)

Used to encapsulate peer-to-peer messages forwarded through the relay:

```text
Byte offset:
00: 0x00 (Forward envelope indicator)
01: format_flag (uint8_t)
    0x00 = Binary data / Multipart / ASCII command strings
    0x01 = UTF-16LE encoded Unicode text command
02..05: destination_ip (4 bytes, IPv4 REVERSED: d.c.b.a)
06..07: destination_port (uint16_le)
08..  : application_payload (remaining bytes in frame)
```

- **Reversed IPv4 Encoding**: An IP address `A.B.C.D` is encoded as bytes `D, C, B, A`. For example, `192.0.2.10` becomes `0x0A, 0x02, 0x00, 0xC0`.
- **Destination Target**:
  - The client targets the partner's public IP and target port obtained from partner lookup.
  - When the partner sends packets to the client, the relay stamps the partner's source IP and port into this header.
  - If the host triggers a relay switch (Command 26), the client updates its destination IP/port to match the new envelope header.

---

## 3. Command Channel

### 3.1 Legacy 3DES Cipher (Server Cipher)

Requests to `functions3.ultraviewer.net` (Registration, Lookup, Keepalive) are encrypted with Triple-DES (3DES-EDE3-CBC) with PKCS#7 padding:

- **Key Derivation**:
  $$\text{Key} = \text{SHA1}(\text{UTF-16LE}(\text{password})) \mathbin{\Vert} \text{0x00 0x00 0x00 0x00} \quad (24\text{ bytes})$$
- **IV Derivation**:
  $$\text{IV} = \text{SHA1}(\text{""})[0..7] \quad (8\text{ bytes}) = \text{da 39 a3 ee 5e 6b 4b 0d}$$
- **Default Server Password**: The standard fixed password consists of 5 control characters:
  $$U+0005, U+000A, U+000F, U+0014, U+0019$$
  Raw UTF-16LE bytes: `05 00 0A 00 0F 00 14 00 19 00`.
- **Ciphertext Framing**:
  Encrypted requests are sent as:
  ```text
  [ 4-byte LE length ] [ 0x01 ] [ 0x00 ] [ 3DES Ciphertext ]
  ```
- **Server Responses**:
  Responses from `functions3.ultraviewer.net` are **NOT** encrypted. They arrive as:
  ```text
  [ 4-byte LE length ] [ 0x01 ] [ Plaintext ASCII Text ]
  ```

### 3.2 Registration (Command `3`)

Before querying partner details, a client registers itself to receive an assigned ID and port:

```text
3:<device_id>:<reg_pw>:0:<local_ip>:0:<b64_host>:6.6.133:<mac>::<os>:0::<proof>:0::<user>:0:0:0:0:0:0:0:0:::
```

Exactly 28 colon-separated fields:

| Field Index | Name | Description |
|---|---|---|
| 0 | `cmd` | Constant `"3"` |
| 1 | `device_id` | 32-character uppercase hex string (persistent client install UUID) |
| 2 | `reg_pw` | 8 ASCII numeric digits (client PIN/password) |
| 3 | `reserved` | `"0"` |
| 4 | `local_ip` | Client local LAN IPv4 (e.g. `"192.168.1.50"`) |
| 5 | `reserved` | `"0"` |
| 6 | `computer_name` | Base64-encoded UTF-8 hostname |
| 7 | `client_version`| Client version string, e.g. `"6.6.133"` |
| 8 | `mac_address` | 12 uppercase hex characters (no colons/dashes) |
| 9 | `empty` | `""` |
| 10 | `os_version` | OS string: `Build\|Major\|0\|Minor\|0\|0` |
| 11 | `reserved` | `"0"` |
| 12 | `empty` | `""` |
| 13 | `custom_proof` | Optional custom password proof (empty for standard viewer) |
| 14 | `reserved` | `"0"` |
| 15 | `empty` | `""` |
| 16 | `username` | Local OS user account name |
| 17..24 | `zeros` | Eight `"0"` fields (`0:0:0:0:0:0:0:0`) |
| 25..27 | `trailing` | Three empty fields (`:::`) |

> [!IMPORTANT]
> **MAC Address Isolation**: The UltraViewer server maps registrations directly to the reported MAC address. If a viewer sends the real physical MAC of a machine that also hosts an UltraViewer server instance, the viewer will overwrite the host's credentials. The plugin generates a stable, locally-administered MAC derived deterministically from the `device_id`.

**Server Reply (Command 3)**:
```text
3:<public_port>:<assigned_id>:<public_ip>:...:<status>:...:<ping_list>:...:<relay_hostname>:<server_token>:...
```
Key fields in response:
- Index 1: Public port.
- Index 2: Assigned UltraViewer ID (strip any leading zeros).
- Index 3: Public IPv4 address.
- Index 10: Opaque server token (required for partner lookup).

### 3.3 Partner Lookup (Command `8`)

Used to resolve the target partner ID and acquire relay credentials:

```text
8:<target_id>:<target_pw_md5>:6.6.133:::<assigned_id>:<local_pw_md5>:-1:<corr_id>:0:0:0:<tz_offset>:1::
```

Exactly 17 colon-separated fields:

| Field Index | Name | Description |
|---|---|---|
| 0 | `cmd` | Constant `"8"` |
| 1 | `target_id` | Remote partner UltraViewer ID (numeric string) |
| 2 | `target_pw_md5` | `MD5(" " * 32 + partner_password)` (32 space characters prepended) |
| 3 | `version` | `"6.6.133"` |
| 4..5 | `licenses` | Empty (`::`) |
| 6 | `assigned_id` | Local assigned ID from Command 3 |
| 7 | `local_pw_md5` | `MD5(assigned_id + registration_password)` |
| 8 | `unknown` | `"-1"` |
| 9 | `corr_id` | Random positive 31-bit integer correlation ID |
| 10 | `reserved` | `"0"` |
| 11 | `referral` | `"0"` |
| 12 | `payment` | `"0"` |
| 13 | `tz_offset` | Local timezone offset in minutes east of UTC (e.g. `420` for UTC+7) |
| 14 | `dict_count` | `"1"` |
| 15 | `empty` | `""` |
| 16 | `direct_opts` | Direct P2P negotiation parameters (empty for relay mode) |

**Server Reply (Command 8)**:
```text
8:<status>:<lan_ip>:...:<public_ip>:...:<target_port>:...:<b64_name>:...:<relay_ip>:...:<relay_hostname>:...:<secret_token>:...:<corr_id>
```
Field mapping:
- Index 1: Status code:
  - `1`: Success, credentials valid.
  - `2`: Wrong partner password.
  - `0` / `-1`: Partner ID is offline.
  - Other: Lookup rejected.
- Index 4: Remote partner public IPv4.
- Index 9: Remote partner target port.
- Index 11: Designated relay server IPv4 address.
- Index 14: Relay server TLS hostname (for certificate verification).
- Index 20: Per-session secret token.
- Last Index: Correlation ID (must match request `corr_id`).

### 3.4 Command Keepalive

Every 60 seconds on the command channel, the client writes:
```text
[ 4-byte LE length ] [ 0x01 ] [ 0x00 ] [ 3DES("2") ]
```
Wire bytes: `0a 00 00 00 01 00 2d 93 29 71 dd 9d cb 2d`.

---

## 4. Relay Session Authentication

### 4.1 Partner Login Message

Sent inside a forward envelope (`format_flag = 0`) to `partner_ip:partner_port`:

```text
login:<nonce>:<pw_proof>:<width>:<height>:0:15:15:0:0:<local_id>:<display_name>:<control_mode>:<pw_proof32>::<conn_hash>:2:45:0:0:<secret_token>:6.6.133:0
```

23 colon-separated fields:

| Field Index | Description | Example / Value |
|---|---|---|
| 0 | Command keyword | `"login"` |
| 1 | Random Nonce | 8 ASCII numeric digits |
| 2 | Nonce proof | `MD5(nonce + partner_password)` |
| 3 | Local screen width | `"1920"` |
| 4 | Local screen height | `"1080"` |
| 5..9 | Display metrics | `0:15:15:0:0` |
| 10 | Local assigned ID | Client ID from Command 3 |
| 11 | Display name | Sanitized ASCII string (max 20 chars, e.g. `"Remmina"`) |
| 12 | Control mode | `"0"` initially; `"-1"` after session is active |
| 13 | Password proof 32 | `MD5(" " * 32 + partner_password)` |
| 14 | Empty | `""` |
| 15 | Connection hash | 16-character alphanumeric random token (session-stable) |
| 16 | Color depth mode | `"2"` (modern RGB555 mode) |
| 17 | Display quality | `"45"` (default balanced quality) |
| 18..19 | Reserved | `0:0` |
| 20 | Secret token | Partner secret token from Command 8 |
| 21 | Version | `"6.6.133"` |
| 22 | Reserved | `"0"` |

### 4.2 Acceptance Handshake

UltraViewer has no explicit `"login_ok"` response. Acceptance is proven when the host transmits **Command 0 (Screen Initialization)**. If Command 0 is not received within 20 seconds, the connection attempt is aborted.

---

## 5. Session Lifecycle & Command Dispatch

### 5.1 Complete Command Matrix

Peer application messages are either multipart payloads (`part[0][0] = command`) or delimited text commands:

| Command | Direction | Encoding | Meaning |
|---|---|---|---|
| `0` | Host → Viewer | Multipart | **Screen Initialization**: screen geometry, monitor array, tile size |
| `1` | Host → Viewer | Multipart | **Screen Frame**: rects, zstd pixels, JPEG atlas, tile cache ops |
| `3` | Host → Viewer | Multipart | **Cursor Shape Selection**: 32-bit hash of active cursor |
| `4` | Host → Viewer | Multipart | **Cursor Shape Transfer**: zstd-compressed Windows `.cur` data |
| `5` | Both | UTF-16LE text | **Legacy Clipboard**: uncompressed Unicode text (`5\|\|\|text`) |
| `12` | Both | Text / Multipart | **In-Session Chat**: message text (`12\|\|\|text`) |
| `13` | Both | Text / Multipart | **File Transfer Offer**: file name, size, transfer ID |
| `14` | Both | Text / Multipart | **File Transfer Accept**: accept file offer |
| `15` | Both | Text / Multipart | **File Transfer Chunk**: data payload chunk |
| `16` | Both | Text / Multipart | **File Transfer Cancel**: cancel ongoing transfer |
| `17` | Viewer → Host | ASCII text | **Screen Init Ack**: viewer is ready for frames (`"17"`) |
| `18` | Viewer → Host | ASCII text | **Frame Ack**: viewer decoded frame, ready for next (`"18"`) |
| `19` | Viewer → Host | ASCII text | **Disconnect Notice**: viewer is terminating session (`"19"`) |
| `20` | Viewer → Host | ASCII text | **Peer Heartbeat**: sent every 10 seconds (`"20"`) |
| `21` | Both | Text / Multipart | **File Chunk Ack**: window flow control confirmation |
| `22` | Viewer → Host | UTF-16LE text | **Action**: Send Ctrl+Alt+Delete |
| `23` | Both | Text / Multipart | **Host Re-Login Request** / **Wallpaper Toggle** |
| `26` | Host → Viewer | Multipart | **Relay Switch**: partner moved to new relay / endpoint |
| `27` | Host → Viewer | Multipart | **Remote Disconnected**: host closed session gracefully |
| `28` | Both | ASCII / Multipart | **Cursor Toggle** (`28\|\|\|1`) / **Pointer Position** (`x, y`) |
| `29` | Both | Text / Multipart | **Chat Typing Started** |
| `30` | Both | Text / Multipart | **Chat Typing Stopped** |
| `31` | Viewer → Host | ASCII text | **Display Quality**: change compression quality (`31\|\|\|<val>`) |
| `32` | Viewer → Host | ASCII text | **Color Mode**: request RGB555 mode (`32\|\|\|2`) |
| `33` | Both | Multipart / Hybrid | **Modern Clipboard**: zstd-compressed UTF-16LE text |
| `39` | Host → Viewer | Multipart | **Connection Blocked**: host rejected or blacklisted connection |
| `40` | Viewer → Host | UTF-16LE text | **Action**: Reboot remote computer |
| `41` | Host → Viewer | Multipart | **Host Session Terminated** |
| `42` | Host → Viewer | Multipart | **Audio Silence**: PCM zero byte count |
| `43` | Host → Viewer | Multipart | **Audio Stream**: raw G.722 encoded audio frames |
| `46` | Viewer → Host | UTF-16LE text | **Audio Toggle**: enable/disable sound (`46\|\|\|-1` / `46\|\|\|0`) |
| `48` | Host → Viewer | Multipart | **Tile Size Changed**: byte payload indicating new tile size |
| `49` | Host → Viewer | Multipart | **View-Only State**: byte payload (0 = full control, 1 = view-only) |
| `53` | Viewer → Host | UTF-16LE text | **Action**: Lock remote workstation |
| `54` | Both | Text / Multipart | **Side Switch Request** / Reverse control negotiation |

### 5.2 Re-Login & Relay Switching

- **Command 23 (Re-login)**: If the host issues Command 23, the viewer immediately transitions to inactive and transmits a fresh login message with `control_mode = -1`.
- **Command 26 (Relay Switch)**: Part 1 contains the session connection hash. If it matches, the viewer updates its destination relay IP/port to match the incoming forward envelope header, discards its current renderer state, and transmits a fresh login with `control_mode = -1` and `clear_control = true`.

---

## 6. Screen Initialization (Command `0`)

Sent by the host as a multipart payload to establish virtual desktop dimensions and parameters:

| Part Index | Data Type | Description |
|---|---|---|
| 0 | `uint8` | Constant `0x00` |
| 1 | `int32_le` | Virtual desktop width (pixels) |
| 2 | `int32_le` | Virtual desktop height (pixels) |
| 6 | ASCII string | Host UltraViewer version (e.g. `"6.6.133"`) |
| 8 | ASCII string | Monitor geometry list: `left:top:right:bottom\|...\|` |
| 9 | `int32_le` | Virtual desktop origin X offset |
| 10 | `int32_le` | Virtual desktop origin Y offset |
| 14 | `uint8` | Baseline tile size (default `16`, pixels) |

**Viewer Initialization Routine**:
1. Validate geometry: width and height must be $> 0$, $\le 16384$, total pixels $\le 32,000,000$.
2. Instantiate screen renderer with baseline frame sequence `last_frame_number = 0x80000001`.
3. Allocate Cairo 32-bit `RGB24` image surface (`width × height`).
4. Transmit ASCII `"17"` (Initialization Ack).
5. Transmit ASCII `"28|||1"` (Enable remote cursor updates).
6. Begin accepting Command 1 screen frames.

---

## 7. Screen Frame Updates (Command `1`)

Delivered as an 11-part multipart payload:

| Part | Type | Format / Records | Meaning |
|---|---|---|---|
| 0 | `uint8` | `0x01` | Screen update command byte |
| 1 | Rects | `uint16_le x, y, w, h` (8 bytes/record) | Main raw rectangles |
| 2 | Compressed | Zstd byte stream | Decompressed into raw RGB555 pixels for Part 1 |
| 3 | Unused | Empty or ignored | Reserved |
| 4 | `int32_le` | 4 bytes | Monotonic frame sequence number |
| 5 | Rects | `int16_le x, y, w, h` (8 bytes/record) | Copy-from-temporary rectangles |
| 6 | Scroll | Header (10 bytes) + Rects | Vertical scroll delta, capture rect, copy-back rects |
| 7 | JPEG | Standard JPEG image stream | Atlas for auxiliary/complex tiles |
| 8 | Rects | `int16_le x, y, w, h` (8 bytes/record) | Target rectangles for JPEG atlas |
| 9 | Cache Refs| `int16_le x, y`, `int32_le hash` (8 bytes) | Tile cache lookup & copy to temp |
| 10 | Cache Adds| `int16_le x, y, w, h`, `int32_le hash` (12 bytes) | Regions to store in tile cache |

### Frame Sequence Validation
- Frame sequence numbers start at `0x80000001` (`-2147483647`).
- The first frame must be `0x80000002` (`-2147483646`).
- Subsequent frames must increment strictly by 1: `current == previous + 1` (using signed 32-bit modular arithmetic).
- If a sequence gap occurs, the frame is dropped and the viewer triggers a full screen refresh.

---

## 8. Framebuffer & Rendering Pipeline

### 8.1 Buffer Layouts

The renderer manages three memory buffers:
1. **Main Framebuffer**: Cairo 32-bit `CAIRO_FORMAT_RGB24` surface (`width × height`). Pixels are native 32-bit words `0x00RRGGBB`.
2. **Temporary Staging Buffer (`temp`)**: Bottom-up 16-bpp RGB555 buffer. Row stride is DWORD-aligned:
   $$\text{stride} = (\text{width} \times 2 + 3) \ \& \ \sim 3$$
3. **Tile Cache**: LRU hash map keyed by signed 32-bit integer hash (`int32_t`). Stores uncompressed RGB555 tile buffers (capacity: 1,000 tiles or 64 MiB).

### 8.2 Frame Decode Steps

Updates within a frame are applied in strict order:

```
┌─────────────────────────────────────────────────────────────┐
│ 1. Scroll: Blit main -> temp (with delta), temp -> main     │
├─────────────────────────────────────────────────────────────┤
│ 2. Cache References: Blit cached tiles into temp            │
├─────────────────────────────────────────────────────────────┤
│ 3. Copy-from-Temporary: Copy regions from temp -> main      │
├─────────────────────────────────────────────────────────────┤
│ 4. JPEG Atlas: Decode JPEG, unpack tiles directly -> main   │
├─────────────────────────────────────────────────────────────┤
│ 5. Raw Rectangles: Decompress Zstd -> temp, temp -> main    │
├─────────────────────────────────────────────────────────────┤
│ 6. Cache Additions: Extract regions from main -> tile cache │
└─────────────────────────────────────────────────────────────┘
```

**Backpressure Acknowledgment**: After completing the rendering pipeline for a frame, the viewer immediately writes ASCII `"18"` back to the host. The host throttles frame generation until Command 18 is received.

### 8.3 RGB555 Color Space Conversion

UltraViewer transmits 16-bpp pixels as 15-bit RGB (`5-5-5`), with bit 15 ignored:

```c
static inline uint32_t rgb555_to_xrgb8888(uint16_t v) {
    uint32_t r = (v & 0x1F);
    uint32_t g = (v >> 5) & 0x1F;
    uint32_t b = (v >> 10) & 0x1F;
    uint32_t r8 = (r << 3) | (r >> 2);
    uint32_t g8 = (g << 3) | (g >> 2);
    uint32_t b8 = (b << 3) | (b >> 2);
    return (r8 << 16) | (g8 << 8) | b8;
}
```

### 8.4 JPEG Atlas Tile Extraction

Part 7 contains a standard JFIF/JPEG image acting as a tile atlas:
- Decoded into an uncompressed RGB bitmap via `libjpeg-turbo`.
- Tiles are addressed in column-major order: outer loop steps X across the destination auxiliary rectangle, inner loop steps Y down the column in increments of `tile_size`.
- Source rows are addressed bottom-up:
  $$\text{atlas\_row} = \text{atlas.height} - 1 - \text{source\_y} - \text{row}$$

---

## 9. Zstandard Stream & Dictionary Specification

Screen pixel data in Part 2 is compressed using Zstandard (zstd) with a custom pre-trained dictionary:

- **Dictionary Asset**: `8bit.dat`
- **File Size**: Exactly `112,640` bytes (`0x1B800` bytes).
- **SHA-256 Digest**: `9aa45789d9ff9d329a623a685d57b67e33c777019de9ce1f273f21860e4bfe6b`
- **Zstd Dictionary ID**: `0x3da0bd10`
- **Decompression Routine**:
  - Requires `ZSTD_createDCtx()` and `ZSTD_DCtx_loadDictionary()`.
  - The dictionary is loaded once at session startup.
  - Streaming decompression (`ZSTD_decompressStream`) is reset at the start of each frame using `ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only)`.
  - Total expected decompressed bytes must exactly match:
    $$\sum (\text{rect.width} \times \text{rect.height} \times 2)$$
  - Any mismatch triggers immediate frame rejection and session refresh.

---

## 10. Remote Cursor Protocol

UltraViewer provides a complete remote hardware cursor system, handling both pointer movement and dynamic cursor shape modifications.

### 10.1 Cursor Position Updates (Command `28`)

- **Enabling Cursor Updates**: The viewer sends ASCII `"28|||1"` (format flag 0) once after Command 0. The host then begins broadcasting cursor movement.
- **Position Wire Payload**: Host sends multipart Command 28:
  - `part[0]`: Command byte `0x1C` (28).
  - `part[1]`: Signed 16-bit integer `x` coordinate (`int16_le`).
  - `part[2]`: Signed 16-bit integer `y` coordinate (`int16_le`).
- **Coordinate System**: Values are in virtual desktop coordinates and must have virtual desktop origin (`virtualLeft`, `virtualTop`) subtracted before positioning on the local canvas.

### 10.2 Cursor Shape Selection (Command `3`)

When the remote mouse cursor changes shape to a previously seen or standard shape:
- Host sends multipart Command 3:
  - `part[0]`: Command byte `0x03`.
  - `part[1]`: Signed 32-bit hash (`int32_le`).
- If the hash matches the special value `0xb84d1a70` (`ignoredSelectionHash`), the selection update is discarded.
- Otherwise, the client looks up the hash in its cursor cache and activates the corresponding `GdkCursor`.

### 10.3 Cursor Shape Transfer (Command `4`)

When the host introduces a new cursor shape:
- Host sends multipart Command 4:
  - `part[0]`: Command byte `0x04`.
  - `part[1]`: Signed 32-bit hash (`int32_le`).
  - `part[2]`: Uncompressed byte length (`int32_le`).
  - `part[3]`: Zstandard-compressed payload.
- **Decompression**: Decompressed with standard Zstandard **WITHOUT any dictionary** (`ZSTD_createDCtx()`).
- **Decompressed Content**: A complete binary Windows `.cur` cursor file.

### 10.4 Windows `.CUR` File Structure

The decompressed buffer conforms to the standard Windows Cursor resource format:

```text
ICONDIR Header (6 bytes):
  uint16_le idReserved   # Must be 0
  uint16_le idType       # Must be 2 for CUR (1 = ICO, 2 = CUR)
  uint16_le idCount      # Number of images (usually 1)

ICONDIRENTRY Record (16 bytes per image):
  uint8_t   bWidth       # Width in pixels (0 represents 256)
  uint8_t   bHeight      # Height in pixels (0 represents 256)
  uint8_t   bColorCount  # Color palette count (0 if >= 8bpp)
  uint8_t   bReserved    # Must be 0
  uint16_le wXHotspot    # Hotspot X offset from left edge
  uint16_le wYHotspot    # Hotspot Y offset from top edge
  uint32_le dwBytesInRes # Total resource data byte size
  uint32_le dwImageOffset# Absolute byte offset of image data in file
```

- **Image Resource**: Contains a `BITMAPINFOHEADER` followed by DIB color data and XOR/AND masks, or an embedded PNG file.
- **Conversion**: The plugin decodes the bitmap into a 32-bit Cairo image surface and instantiates a native `GdkCursor` via `gdk_cursor_new_from_surface(gdk_display_get_default(), surface, wXHotspot, wYHotspot)`.

---

## 11. User Input Encoding

### 11.1 Mouse Events

Mouse messages are formatted as delimited ASCII strings within a forward envelope (`format_flag = 0`):

| Windows Event | Format String | Description |
|---|---|---|
| `WM_MOUSEMOVE` | `8\|\|\|512\|\|\|<flags>\|\|\|<x>\|\|\|<y>` | Pointer movement |
| `WM_LBUTTONDOWN` | `8\|\|\|513\|\|\|<flags>\|\|\|<packed>` | Left button press |
| `WM_LBUTTONUP` | `8\|\|\|514\|\|\|<flags>\|\|\|<packed>` | Left button release |
| `WM_RBUTTONDOWN` | `8\|\|\|516\|\|\|<flags>\|\|\|<packed>` | Right button press |
| `WM_RBUTTONUP` | `8\|\|\|517\|\|\|<flags>\|\|\|<packed>` | Right button release |
| `WM_MBUTTONDOWN` | `8\|\|\|519\|\|\|<flags>\|\|\|<packed>` | Middle button press |
| `WM_MBUTTONUP` | `8\|\|\|520\|\|\|<flags>\|\|\|<packed>` | Middle button release |
| `WM_MOUSEWHEEL` | `8\|\|\|522\|\|\|<wParam>\|\|\|<packed>` | Vertical scroll wheel |

- **Flags (`MK_*`)**: Left `0x01`, Right `0x02`, Shift `0x04`, Control `0x08`, Middle `0x10`.
- **Packed Coordinates**:
  $$\text{packed} = (\text{x} \ \& \ \text{0xFFFF}) \mid ((\text{y} \ \& \ \text{0xFFFF}) \ll 16)$$
  Formatted as a signed 32-bit decimal integer.
- **Wheel Delta**: Standard notch is $\pm 120$ (positive = up).
  $$\text{wParam} = ((\text{wheel\_delta} \ \& \ \text{0xFFFF}) \ll 16) \mid \text{flags}$$
- **Desktop Offsets**: Coordinates must include virtual desktop origin:
  $$\text{wire\_x} = \text{canvas\_x} + \text{virtualLeft}, \quad \text{wire\_y} = \text{canvas\_y} + \text{virtualTop}$$

### 11.2 Keyboard Events (Binary Mode)

For hosts running UltraViewer $\ge 6.2.13$, keystrokes are transmitted as fixed 11-byte binary records within a forward envelope (`format_flag = 0`):

```text
Byte offset:
00..03: 0x1E 0x7C 0x7C 0x7C  (ASCII prefix: 30, '|', '|', '|')
04    : kind (uint8_t)
        0x01 = Key Down (standard)
        0x02 = Key Up (standard)
        0x03 = System Key Down (Alt held or Alt key itself)
        0x04 = System Key Up
05    : vk_code (uint8_t)    # Windows Virtual Key code
06..07: scan_code (uint16_le)# Hardware scan code (Set 1), high byte 0xE0 for extended
08..09: char_code (uint16_le)# UTF-16 code unit (for text input) or 0
10    : caps_lock (uint8_t)  # 1 if CapsLock active, 0 otherwise
```

- **Unicode Text Input**: Arbitrary Unicode characters not present on a standard US keyboard can be injected using `VK_PACKET` (`0xE7`) with the UTF-16 code point in `char_code` and `scan_code = 0`.
- **System Keys**: Keystrokes while `Alt` is pressed (e.g. `Alt+Tab`, `Alt+F4`) must use `kind = 3` (down) and `kind = 4` (up).

### 11.3 Legacy Host Keyboard Limitations

Hosts running version $< 6.2.13$ do not implement binary Command 30 parsing. The viewer gracefully drops keyboard events when connected to legacy hosts while preserving mouse input.

---

## 12. Text Clipboard Synchronization

### 12.1 Modern Compressed Mode (Command `33`)

- **Viewer → Host**:
  1. Convert text to UTF-16LE.
  2. Compress UTF-16LE bytes with Zstandard (dictionary-free).
  3. Prepend ASCII header `"33|||<utf16_byte_count>|||"`.
  4. Send inside forward envelope with `format_flag = 0`.
  *(Note: If text is empty, the host requires fallback to legacy Command 5 `5|||` with `format_flag = 1`).*
- **Host → Viewer**:
  Delivered as multipart Command 33 (`format_flag = 0`):
  - `part[0]`: Command byte `0x21` (33).
  - `part[1]`: Expected uncompressed byte count (`uint32_le`).
  - `part[2]`: Zstandard-compressed UTF-16LE payload.

### 12.2 Legacy Fallback (Command `5`)

- Sent as UTF-16LE text inside forward envelope with `format_flag = 1`:
  $$\text{UTF-16LE}("5|||" + \text{text})$$
- Only supported when text contains no `|||` delimiters.

### 12.3 Echo Suppression & Guard Windows

To prevent infinite clipboard update reflection loops between local and remote X11/Wayland clipboards, the plugin tracks a high-resolution timestamp (`clipboard_timer`). Local clipboard change signals occurring within 1,000 ms of applying a remote update are discarded.

---

## 13. In-Session Chat Protocol

### 13.1 Message Format (Command `12`)

UltraViewer includes built-in two-way text chat:

- **Viewer → Host**:
  Sent inside forward envelope with `format_flag = 1` (Unicode text):
  $$\text{UTF-16LE}("12|||" + \text{message\_text})$$
  - Delimiter protection: `message_text` must not contain `|||`.
  - Length limit: Maximum 32,768 bytes UTF-16LE.
- **Host → Viewer**:
  Delivered as multipart Command 12 with `format_flag = 0`:
  - `part[0]`: Command identifier (`0x0C` or `[0x0C, 0x00]` Int16LE).
  - `part[1]`: UTF-16LE encoded message text.

### 13.2 Typing Indicator (Commands `29` & `30`)

- **Typing Started**:
  - Viewer → Host: UTF-16LE `"29"` (`format_flag = 1`).
  - Host → Viewer: Multipart with `part[0] = 29`.
- **Typing Stopped**:
  - Viewer → Host: UTF-16LE `"30"` (`format_flag = 1`).
  - Host → Viewer: Multipart with `part[0] = 30`.

---

## 14. File Transfer Protocol

UltraViewer implements a chunked file transfer protocol allowing bidirectional file exchange independent of the remote screen channel.

### 14.1 Transfer Sequence & Chunking

- Maximum chunk payload size: **32,000 bytes**.
- Maximum file size: `999,999,999,999,999` bytes.
- Flow control: Every chunk transmitted requires a corresponding Chunk Received Ack (`21`) before the next chunk window advances.

### 14.2 Wire Messages (Commands 13, 14, 15, 16, 21)

#### 1. File Offer (Command `13`)
- **Viewer → Host** (`format_flag = 0`):
  ```text
  13|||<transferID>|||<fileName>|||<fileSize>
  ```
- **Host → Viewer** (Multipart):
  - `part[0]`: Command `[13, 0]` (`int16_le`).
  - `part[1]`: Transfer ID (ASCII string).
  - `part[2]`: File name (ASCII / Windows CP1252 string).
  - `part[3]`: File size (`double_le`, 8 bytes IEEE 754 float).

#### 2. File Accept (Command `14`)
- **Viewer → Host**: `14|||<transferID>|||1`
- **Host → Viewer**: Multipart `[ [14, 0], transferID, [1, 0, 0, 0] ]`

#### 3. File Chunk (Command `15`)
- **Viewer → Host**:
  ```text
  15|||<transferID>|||<byte_offset>|||<raw_chunk_bytes>
  ```
- **Host → Viewer** (Multipart):
  - `part[0]`: Command `[15, 0]` (`int16_le`).
  - `part[1]`: Transfer ID.
  - `part[2]`: Byte offset (`double_le`, 8 bytes).
  - `part[3]`: Raw binary chunk bytes (up to 32,000 bytes).

#### 4. File Cancel (Command `16`)
- **Viewer → Host**: `16|||<transferID>`
- **Host → Viewer**: Multipart `[ [16, 0], transferID ]`

#### 5. Chunk Received Acknowledgment (Command `21`)
- **Viewer → Host**: `21|||<transferID>`
- **Host → Viewer**: Multipart `[ [21, 0], transferID ]`

---

## 15. Computer Audio Streaming

### 15.1 Audio Activation (Command `46`)

To capture and forward remote Windows system audio to the Linux client:
- **Enable Audio**: Viewer sends UTF-16LE `"46|||-1"` (`format_flag = 1`).
- **Disable Audio**: Viewer sends UTF-16LE `"46|||0"` (`format_flag = 1`).

### 15.2 G.722 Audio Stream (Command `43`) & Silence (Command `42`)

When enabled, the host transmits audio data over the relay channel:

- **Audio Data (Command `43`)**:
  Multipart payload (`format_flag = 0`):
  - `part[0]`: Command byte `0x2B` (43).
  - `part[1]`: Raw ITU-T G.722 encoded speech/audio packet (16 kHz sampling rate, 64 kbps bit rate).
  - The client decodes G.722 frames to 16-bit linear PCM and outputs to ALSA/PulseAudio/PipeWire.
- **Audio Silence (Command `42`)**:
  Multipart payload:
  - `part[0]`: Command byte `0x2A` (42).
  - `part[1]`: `int32_le` count of silence PCM bytes.
  - Silence packets allow the client audio sink to maintain clock synchronization without advancing G.722 decoder state.

---

## 16. Remote Control Actions & Session Tools

### 16.1 System Actions (CAD, Reboot, Lock)

Executed by transmitting short UTF-16LE action codes inside forward envelopes (`format_flag = 1`):

- **Send Ctrl+Alt+Delete**: UTF-16LE `"22"`. Host generates a secure SAS (Secure Attention Sequence).
- **Restart Remote Computer**: UTF-16LE `"40"`.
- **Lock Remote Workstation**: UTF-16LE `"53"`.

### 16.2 Quality & Display Settings (Commands `31` & `32`)

- **Change Display Quality (Command `31`)**:
  Viewer sends ASCII `"31|||<quality_val>"` (`format_flag = 0`):
  - Speed Preset: `30`
  - Balanced / Auto: `45`
  - High Quality: `70`
- **Force RGB555 Color Mode (Command `32`)**:
  Viewer sends ASCII `"32|||2"` (`format_flag = 0`).

### 16.3 Desktop Wallpaper Toggle (Command `23`)

- **Show Wallpaper**: UTF-16LE `"23|||-1"` (`format_flag = 1`).
- **Hide / Blank Wallpaper**: UTF-16LE `"23|||0"` (`format_flag = 1`). Used to conserve bandwidth on slow connections.

### 16.4 Multi-Monitor Desktop Layout

Command 0 Part 8 transmits monitor geometries as a formatted string:
$$\text{left}_1:\text{top}_1:\text{right}_1:\text{bottom}_1 \ | \ \text{left}_2:\text{top}_2:\text{right}_2:\text{bottom}_2 \ | \ \dots \ |$$
- Coordinates are signed 32-bit integers in virtual desktop pixel space.
- Individual monitors can be selected locally by cropping the Cairo viewport to the corresponding rectangle without requiring remote renegotiation.

### 16.5 Reverse Control / Side Switch (Command `54`)

Allows switching control direction (letting the host control the viewer):
- Request: Viewer sends UTF-16LE `"54|||<local_id>|||<password>"`.
- Ack: Host sends multipart `[ [54] ]`.

---

## 17. Direct LAN P2P Mode

When both machines are on the same local subnet or have open ports, the relay lookup response (Command 8) provides LAN endpoint parameters in field 21 (`lan_ip:port:...`).

- **Connection Attempt**: Client opens a raw TCP connection to `lan_ip:port` with a 2-second timeout budget (`P2PTryConnect`).
- **Direct Framing**: Frames use 4-byte LE length framing encrypted with **AES-128-CBC** (`LegacyAES`), using a key derived from the session secret token.
- **Relay Fallback**: If the direct TCP handshake fails or times out, the client falls back to the TLS relay channel seamlessly.

---

## 18. Asset Management & Setup Installer Extraction

### 18.1 Required Asset: `8bit.dat`

The UltraViewer Zstandard screen decompressor strictly requires `8bit.dat` (112,640 bytes). Because this dictionary is an UltraViewer proprietary data asset and not licensed under open source terms, the plugin provides an automated extraction script rather than committing the raw binary into git.

### 18.2 Extraction from Official Installer

- **Official Installer URL**: `https://dl2.ultraviewer.net/UltraViewer_setup_6.6.133_vi.exe`
- **Installer Type**: Inno Setup archive.
- **Extraction Tools**: Can be extracted on Linux using `innoextract` or `7z`:
  ```bash
  # Using innoextract
  innoextract -e -I "app/8bit.dat" UltraViewer_setup_6.6.133_vi.exe
  # Or using 7z
  7z e UltraViewer_setup_6.6.133_vi.exe "8bit.dat" -r
  ```
- **Checksum Verification**:
  ```bash
  sha256sum 8bit.dat
  # Expected: 9aa45789d9ff9d329a623a685d57b67e33c777019de9ce1f273f21860e4bfe6b
  ```

---

## 19. Heartbeat, Keepalives & Timeout Budgets

| Interval | Target Channel | Operation | Action on Failure |
|---|---|---|---|
| **10 s** | Relay Channel | Relay Ping: `0x01` + `"ping"` | Mark relay dead; attempt re-login |
| **10 s** | Relay Channel | Peer Heartbeat: Forward env + ASCII `"20"` | Reset idle timer |
| **60 s** | Command Channel| Command Keepalive: `0x01 0x00` + `3DES("2")` | Reconnect command socket |
| **20 s** | Relay Channel | Login Acceptance Deadline (Wait for Cmd 0) | Abort session with timeout error |
| **3 s** | Relay / TCP | TCP Connect Timeout per IP | Try next resolved A record |
| **30 s** | All Sockets | Socket Write / Flush Timeout | Disconnect and clean up |

---

## 20. Security Model, Resource Limits & Error Codes

### 20.1 Protocol Limits
- Frame buffer: Max 16 MiB (`16,777,216` bytes).
- Multipart container: Max 4,096 parts, max 64 MiB total.
- Screen resolution: Dimensions $\le 16,384 \times 16,384$, max $32,000,000$ pixels.
- Tile cache: Max 1,000 entries, max 64 MiB memory budget.
- Clipboard: Max 1 MiB text, max 2 MiB wire payload.
- File transfer: Max 32,000 bytes per chunk.

### 20.2 User-Facing Error Messages

| Condition | Remmina User Notification |
|---|---|
| Command 3 registration timeout | `"UltraViewer did not answer the registration"` |
| Command 8 status = `2` | `"UltraViewer rejected the partner password"` |
| Command 8 status = `0` / `-1` | `"The partner ID is not online"` |
| Command 8 status = other | `"UltraViewer declined the partner lookup (code %d)"` |
| No Command 0 within 20s | `"The partner did not accept the session in time"` |
| Invalid relay TLS cert | `"The relay assigned to this session has an invalid security certificate"` |
| Command 39 received | `"The partner computer blocked this connection"` |
| Commands 27 / 41 / 54 | `"The remote computer disconnected"` |
| Frame decode failures $\ge 2$ | `"The remote screen could not be decoded"` |

---

## 21. Test Vectors & Synthetic Payloads

### 21.1 3DES Command Cipher
- Password: `"fixture-⌘"`
- Plaintext: `"8:123456789:synthetic-proof"`
- Expected Ciphertext (Hex):
  `b63b9797064974fa31181fef729c755558492379dcb44c69e3fe4c6d0e29836c`

### 21.2 Default Server Cipher
- Plaintext: `"8:fixture"`
- Expected Ciphertext (Hex):
  `c842551785856d0b8f866afed8dcd8b1`

### 21.3 Forward Envelope
- Forward to `192.0.2.10:12345`, `format_flag = 0`, Payload `[0xAA, 0xBB]`:
  `00 00 0A 02 C0 39 30 AA BB`

### 21.4 System Keepalives
- Relay ping: `01 70 69 6E 67`
- Command keepalive (`3DES("2")`): `01 00 2D 93 29 71 DD 9D CB 2D`

### 21.5 Password Proofs
- Nonce: `"12345678"`, Password: `"test-pass"`:
  - Field 2 (`MD5(nonce + password)`): `c10f3394b41ec5b86060e0285e0dd306`
  - Field 13 (`MD5(" " * 32 + password)`): `85347ba7a403ffed52d311b7c5b6b549`

---

## 22. MacViewer / LinuxViewer Source Code Map

| Protocol Component | MacViewer Swift Source | LinuxViewer Rust Source |
|---|---|---|
| Client Registration | `Sources/UltraViewerProtocol/ClientRegistration.swift` | `LinuxViewer/src/messages.rs` |
| Partner Lookup | `Sources/UltraViewerProtocol/PeerLookup.swift` | `LinuxViewer/src/command.rs` |
| Command 3DES Cipher | `Sources/UltraViewerProtocol/LegacyCommandCipher.swift` | `LinuxViewer/src/wire.rs` |
| Relay Envelopes & Framing| `Sources/UltraViewerProtocol/RelayEnvelope.swift` | `LinuxViewer/src/wire.rs` |
| TLS & HTTP Preamble | `Sources/UltraViewerProtocol/FramedTCPTransport.swift` | `LinuxViewer/src/tls.rs` |
| Screen Init & Frames | `Sources/UltraViewerProtocol/ModernScreenMessage.swift` | `LinuxViewer/src/screen/frame.rs` |
| Framebuffer Renderer | `Sources/UltraViewerProtocol/ModernScreenRenderer.swift` | `LinuxViewer/src/screen/renderer.rs` |
| Zstandard Screen Decoder| `Sources/UltraViewerProtocol/ZstdScreenDecoder.swift` | `LinuxViewer/src/screen/zstd.rs` |
| Remote Cursor Shapes | `Sources/UltraViewerProtocol/CursorMessage.swift` | `LinuxViewer/src/cursor.rs` |
| Mouse & Keyboard Input | `Sources/UltraViewerProtocol/MouseMessage.swift` / `KeyboardMessage.swift` | `LinuxViewer/src/input.rs` |
| Text Clipboard | `Sources/UltraViewerProtocol/ClipboardProtocol.swift` | `LinuxViewer/src/clipboard.rs` |
| In-Session Chat | `Sources/UltraViewerProtocol/ChatProtocol.swift` | `LinuxViewer/src/chat.rs` |
| File Transfer Engine | `Sources/UltraViewerProtocol/FileTransferEngine.swift` | `LinuxViewer/src/files.rs` |
| Computer Audio Streaming | `Sources/UltraViewerProtocol/ComputerAudioMessage.swift` | `LinuxViewer/src/audio.rs` |
| Remote Control Commands | `Sources/UltraViewerProtocol/RemoteControlCommands.swift` | `LinuxViewer/src/control.rs` |
| Direct LAN P2P | `Sources/UltraViewerProtocol/TCPDirectPeerTransport.swift` | `LinuxViewer/src/direct.rs` |
| Session State Machine | `Sources/UltraViewerProtocol/UltraViewerRelaySession.swift` | `LinuxViewer/src/client.rs` |
