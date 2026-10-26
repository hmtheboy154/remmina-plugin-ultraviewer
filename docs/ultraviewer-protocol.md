# UltraViewer protocol notes

This document records the wire behaviour used by the Remmina UltraViewer plugin.
It is derived from the MacViewer research tree (`Sources/UltraViewerProtocol` and
`LinuxViewer/src`) and is written as an implementation reference, not as a
specification published by UltraViewer.

The plugin is a **viewer/controller**. Hosting an UltraViewer session is out of
scope.

## 1. Transport overview

An UltraViewer viewer uses two TLS connections:

1. **Command channel** to `functions3.ultraviewer.net:443`.
   - Register this computer (`3`), obtain the assigned UltraViewer ID.
   - Look up the partner ID (`8`), obtain the partner's public IP/port, relay
     address and a per-session secret token.
   - Stay open for keepalives and asynchronous server messages.
2. **Relay channel** to the relay address returned by the lookup (normally
   `relay-*.ultraviewer.net:443`, resolved to an IPv4 address).
   - Frames are forwarded to the partner through the relay.
   - The relay does not terminate the UltraViewer protocol; it only routes
     length-prefixed frames to the destination encoded in each frame.

Both connections start with the same pseudo-HTTP preamble and then switch to a
binary length-prefixed framing.

### 1.1 TLS details

- TLS client, certificate verification enabled by default.
- **SNI is always the literal `ultraviewer`**, not the real hostname. The relay
  uses it as a routing label.
- Certificate hostname verification uses the real host:
  - command channel: `functions3.ultraviewer.net`;
  - relay: the `relay_hostname` field from the lookup reply, falling back to
    `functions3.ultraviewer.net`.
- Relays may serve an expired leaf certificate. Windows clients still accept
  it. The reference implementation retries verification at
  `leaf.notAfter - 1` when the current-time check fails with
  `X509_V_ERR_CERT_HAS_EXPIRED` (up to a few times). This plugin retries once.
- The relay may omit the Sectigo intermediate. OpenSSL does not fetch AIA, so
  the plugin embeds:
  - Sectigo Public Server Authentication Root R46
    SHA-256 `7BB647A62AEEAC88BF257AA522D01FFEA395E0AB45C73F93F65654EC38F25A06`
  - Sectigo Public Server Authentication CA DV R36
    SHA-256 `8C54C334B66BA4E426772AF4A3F9136C19A1AEC729FDB28C535C07A5A4EF22E0`
- An explicit "ignore certificate errors" option exists for debugging only.

### 1.2 HTTP preamble

Immediately after the TLS handshake the client writes:

```text
POST /UVService/ HTTP/1.1\r\n
User-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\r\n
Content-Type: application/octet-stream\r\n
Connection: Keep-Alive\r\n
Content-length: 8589934590\r\n
Host: <real host>\r\n
\r\n
```

`Content-length` is intentionally larger than any real body. It is part of the
relay handshake. The server responds with an HTTP header; everything up to and
including the first `\r\n\r\n` is discarded. Bytes after it are the first binary
frame. The HTTP status code is informational; UltraViewer's own clients do not
appear to gate on it.

### 1.3 Frames

After the HTTP header:

```text
uint32_le payload_length
uint8_t   payload[payload_length]
```

Maximum accepted frame size in the reference implementation is 16 MiB. A zero
length frame is legal and should be skipped.

### 1.4 Integer and string conventions

- All protocol integers are little-endian unless stated otherwise.
- Text commands are printable ASCII fields separated by `:`. A field may not
  contain `:`. Empty fields are legal.
- Some application messages use UTF-16LE instead of ASCII; the relay envelope's
  flag byte selects that mode. Login, screen, input and heartbeats are ASCII.
- Multipart application payloads are:

```text
repeat:
  uint32_le part_length
  uint8_t   part_data[part_length]
```

A payload has no part count and no terminator; it ends at the end of the frame.
Reference limits: at most 4096 parts, at most 64 MiB total.

## 2. Relay envelopes

Every relay frame begins with an envelope.

### 2.1 System envelope

```text
0x01 [0x00] payload
```

The optional `0x00` flag byte is present for command-channel messages and the
encrypted command keepalive. It is **absent** for the plain relay `ping`.

Examples:

- relay keepalive: `01 70 69 6E 67` (`"ping"`)
- command keepalive: `01 00 <3DES("2")>`
- server reply on command channel: `01 <ASCII command>`

### 2.2 Forward envelope

```text
0x00
uint8_t  format_flag        # 0 = ASCII/binary application data, 1 = UTF-16LE
uint8_t  ip[4] reversed     # destination IPv4, last octet first
uint16_le port             # destination port
uint8_t  payload[]
```

For `192.0.2.10:12345` the prefix is
`00 00 0A 02 C0 39 30`. The `format_flag` is 0 for screen, input, login and
heartbeat traffic.

To send to the partner, the viewer uses the partner's **public IP and target
port** from the lookup reply. Routed frames received from the partner carry the
partner's source IP/port. If the partner issues a command-26 relay switch, the
viewer updates its destination to the new envelope source.

## 3. Command channel

### 3.1 Command cipher

Registration, lookup and command keepalives are encrypted with 3DES:

```text
key = SHA1(UTF-16LE(password)) || 00 00 00 00      (24 bytes)
iv  = SHA1("")[0..7]                               (8 bytes)
mode = 3DES-EDE3-CBC with PKCS#7 padding
```

The default command password is the five control characters
`U+0005 U+000A U+000F U+0014 U+0019` (bytes `05 0A 0F 14 19` in UTF-16LE:
`05 00 0A 00 0F 00 14 00 19 00`). This is the "server cipher" used for all
clients.

Outgoing encrypted command:

```text
frame( 01 00 <ciphertext> )
```

Server replies are **not** encrypted:

```text
frame( 01 <ASCII reply> )
```

### 3.2 Registration (command `3`)

28 fields, exactly:

| # | Value | Notes |
|---|-------|-------|
| 0 | `3` | command |
| 1 | device identity | 32 uppercase hex chars, stable per install |
| 2 | registration password | 8 ASCII digits, stable per install |
| 3 | `0` | |
| 4 | local IPv4 | e.g. `192.0.2.10` |
| 5 | `0` | |
| 6 | computer name | Base64 of UTF-8 hostname |
| 7 | client version | `6.6.133` |
| 8 | MAC address | 12 uppercase hex chars, no separators |
| 9 | empty | |
| 10 | OS version | `Build\|Major\|0\|Minor\|0\|0`, from uname |
| 11 | `0` | |
| 12 | empty | |
| 13 | custom password proof | normally empty; hosting feature |
| 14 | `0` | |
| 15 | empty | |
| 16 | user name | ASCII, or Base64 of UTF-8 |
| 17-24 | `0` × 8 | |
| 25-27 | empty × 3 | |

Reference text:

```text
3:<identity>:<password>:0:<ip>:0:<b64host>:6.6.133:<mac>::<os>:0::<proof>:0::<user>:0:0:0:0:0:0:0:0:::
```

The relay assigns IDs by **MAC address**. A viewer must not send the real
hardware MAC if a host registration on the same machine could use it, or the
viewer can take over the host's ID/password. Use a stable locally administered
MAC derived from the device identity, as the LinuxViewer viewer profile does.

Reply (plain system payload):

```text
3:<public_port>:<assigned_id>:<public_ip>:...:<status>:...:<ping_list>:...:<relay_hostname>:<server_token>:...
```

Relevant fields:

| Index | Meaning |
|-------|---------|
| 1 | public port, non-zero |
| 2 | assigned ID (may have leading zeroes; parse as number and strip them) |
| 3 | public IPv4 |
| 6 | ping list (hosts to ping) |
| 7 | status (usually `-1` or `0`) |
| 9 | ping host name list |
| 10 | opaque server token |

The reply must be at least 11 fields. If an ID field parses as zero, registration
failed.

### 3.3 Partner lookup (command `8`)

17 fields:

| # | Value | Notes |
|---|-------|-------|
| 0 | `8` | command |
| 1 | target ID | partner UltraViewer ID, digits |
| 2 | target password proof | `md5(32 spaces + partner password)` |
| 3 | client version | `6.6.133` |
| 4 | license | empty |
| 5 | license2 | empty |
| 6 | local ID | assigned ID from registration |
| 7 | local password proof | `md5(local_id + registration_password)` |
| 8 | `-1` | |
| 9 | correlation | random non-zero 31-bit int, echoed by the reply |
| 10 | `0` | |
| 11 | referral | `0` |
| 12 | payment | `0` |
| 13 | timezone offset | minutes east of UTC (e.g. `420`) |
| 14 | dictionary count | `1` |
| 15 | empty | |
| 16 | direct details | empty for relay-only |

Example with empty license/referral/payment/direct fields:

```text
8:<target>:<md5>:6.6.133:::<local_id>:<md5>: -1:<corr>:0:0:0:<tz>:1::
```

(whitespace added above for readability only; the real string has none).

Reply:

```text
8:<status>:<lan_ip>:...:<public_ip>:...:<port>:...:<name_b64>:...:<relay_ip>:...:<relay_hostname>:...:<secret_token>:...
```

Field indexes used:

| Index | Meaning |
|-------|---------|
| 1 | status: `1` = success, `2` = wrong password, `0`/other = offline/declined |
| 4 | partner public IPv4 |
| 9 | partner target port |
| 11 | relay IPv4 |
| 14 | relay hostname (certificate name) |
| 20 | per-session secret token |
| 21 | optional LAN-direct details |

The correlation value is the **last** field in the reply. Ignore replies whose
correlation does not match the request.

### 3.4 Command keepalive

Every 60 seconds send:

```text
frame( 01 00 <3DES("2")> )
```

The command socket is otherwise drained and ignored. Losing it does not end an
established screen session, but failed keepalive writes should shut the session
down if the relay is also unhealthy.

## 4. Relay login

### 4.1 Login message (23 fields)

Sent inside a forward envelope with `format_flag = 0`:

| # | Value | Notes |
|---|-------|-------|
| 0 | `login` | |
| 1 | nonce | exactly 8 ASCII digits |
| 2 | `md5(nonce + password)` | |
| 3 | local screen width | |
| 4 | local screen height | |
| 5 | `0` | |
| 6 | `15` | |
| 7 | `15` | |
| 8 | `0` | |
| 9 | `0` | |
| 10 | local ID | registration ID |
| 11 | display name | sanitized ASCII, max 20 chars |
| 12 | control mode | `0` initially, `-1` after accepted; `0` also clears control |
| 13 | `md5(32 spaces + password)` | |
| 14 | empty | |
| 15 | connection hash | 16 alphanumeric chars, stable for the session |
| 16 | `2` | |
| 17 | `45` | |
| 18 | `0` | |
| 19 | `0` | |
| 20 | partner secret token | from lookup field 20 |
| 21 | client version | `6.6.133` |
| 22 | `0` | |

Display name sanitization: remove `!#$&?:/\*,.@<>;'"=`, fold `đ/Đ` to `d/D`,
keep printable ASCII, trim, max 20 chars, default `Remmina` if empty.

### 4.2 Login acceptance

There is no explicit "accepted" reply. The host proves acceptance by sending a
command-0 screen initialization. The reference client allows 20 seconds and
then fails with "partner did not accept the session in time".

## 5. Session lifecycle messages

Peer application messages are multipart; `part[0][0]` is the command byte.

| Command | Direction | Meaning |
|---------|-----------|---------|
| `0` | host → viewer | screen initialization; accept session |
| `1` | host → viewer | screen frame |
| `17` | viewer → host | initialization acknowledged |
| `18` | viewer → host | frame decoded; request next frame |
| `19` | viewer → host | viewer is leaving |
| `20` | viewer → host | heartbeat, every 10 s |
| `23` | host → viewer | re-login request |
| `26` | host → viewer | relay switch; part 1 is the connection hash |
| `27`, `41`, `54` | host → viewer | remote disconnected |
| `28` | host → viewer | cursor position; part 1/2 are signed 16-bit x/y |
| `39` | host → viewer | connection blocked |
| `48` | host → viewer | tile size change; part 1 byte |
| `49` | host → viewer | view-only state; part 1 byte |

Also sent by the viewer once after the first initialization:

```text
28|||1
```

This is the ASCII input-shaped command that enables cursor-position messages.
The plugin does not currently render remote cursors, so it may omit this.

Relay `ping` (system envelope, no flag) is sent every 10 seconds while the relay
is up. The host answers with a system `pong`, which is ignored.

### 5.1 Re-login

On command `23`, mark the session inactive and send a fresh login with
`control_mode = -1` and `clear_control_mode = false`.

On command `26` (relay switch), verify part 1 equals the connection hash, update
the relay destination to the incoming envelope's IP/port, mark inactive, drop
the renderer and send a fresh login with `control_mode = -1` and
`clear_control_mode = true` (wire mode `0`).

After two consecutive screen-decode failures the reference client also requests
a full refresh by re-login with clear mode. After more failures it disconnects.

## 6. Screen initialization (command `0`)

Multipart fields:

| Part | Meaning |
|------|---------|
| 0 | command byte `0` |
| 1 | width, int32 |
| 2 | height, int32 |
| 5 | must not be empty if present |
| 6 | host UltraViewer version, ASCII (e.g. `6.6.125`) |
| 7 | must not be empty if part 6 is present |
| 8 | monitor rectangles `left:top:right:bottom\|...\|` |
| 9 | virtual desktop origin x, int32 |
| 10 | virtual desktop origin y, int32 |
| 11 | must not be empty if present |
| 12 | at least 2 bytes if present |
| 13 | must not be empty when geometry changes |
| 14 | tile size, first byte, default 16 |

Width/height must be positive, at most 16384 in each dimension and at most
32,000,000 pixels. The plugin only supports the modern 16-bpp pipeline, which
requires host version >= 6.0.0.

The virtual origin is used for input coordinates: remote pointer
position = canvas position + origin. For a single monitor with origin `0,0`
this is a no-op. Monitor rectangles may be absent.

After initialization the viewer:

1. creates/replaces its renderer with `last_frame_number = 0x80000001`;
2. sends ASCII `17`;
3. sends `28|||1` once;
4. starts accepting command-1 frames.

If the size is unchanged, only the tile size is updated. If the size changes,
the renderer is recreated but keeps the previous frame number, because the host
still requires the next frame number to be exactly previous + 1.

## 7. Screen frame (command `1`)

At least 11 parts:

| Part | Type | Meaning |
|------|------|---------|
| 0 | byte | `1` |
| 1 | uint16 ×4 per record | main rectangles (unsigned x, y, width, height) |
| 2 | zstd stream | packed RGB555 pixels for part 1 |
| 3 | unused | |
| 4 | int32 | frame number |
| 5 | int16 ×4 per record | copy-from-temporary rectangles (signed) |
| 6 | scroll | `int16 vertical_delta`, `int16 x,y,w,h` capture, then signed rect records |
| 7 | JPEG | atlas for auxiliary tiles |
| 8 | int16 ×4 per record | auxiliary rectangles (signed) |
| 9 | int16 x, int16 y, int32 hash | cache references |
| 10 | int16 ×4 rect, int32 hash | cache additions |

Part lengths must be exact multiples of their record size (8 bytes for
rectangles and cache refs, 12 for cache adds). The number of records is capped
at 16,384 per part.

Frame numbers start at `0x80000001` (`-2147483647`) as a baseline; the first
frame is `0x80000002` (`-2147483646`). Every subsequent frame must be exactly
`previous + 1` (signed 32-bit arithmetic). A mismatch aborts the frame and
triggers a refresh.

Part 2 decompresses to exactly:

```text
sum(width * height * 2) bytes
```

in the order of the main rectangles. Bytes are little-endian RGB555 words; bit
15 is ignored.

## 8. Rendering pipeline

The renderer keeps:

- `main`: top-down 0x00RRGGBB pixels, `width × height`;
- `temp`: a bottom-up RGB555 staging buffer with DWORD-aligned row stride
  `(width * 2 + 3) & ~3`;
- a tile cache keyed by signed 32-bit hash (max 1000 entries, max 64 MiB).

Application order is important:

1. **Scroll**: copy `capture` from main into temp at
   `(capture.x, capture.y - vertical_delta)`, then copy each `copy_back`
   rectangle from temp into main.
2. **Cache references** write into **temp**, not main. A cache hit also updates
   the entry's recency.
3. **Copy-from-temporary** rectangles copy temp → main.
4. **JPEG atlas tiles** write full-colour RGB directly into main.
5. **Main raw rectangles** are each copied from the decompressed packed pixels
   into temp at `(0,0)`, then temp → main at the rectangle's position.
6. **Cache additions** copy from main into temp, extract the region top-down,
   and store it if the hash is not already cached.

Cache references must hit before any mutation; a miss rejects the whole frame.
The frame is atomic: decode, bounds and budget checks all happen before the
first pixel or cache write.

### 8.1 RectBuffer copy

`copy_rectangle(source, sw, sh, dx, dy)` copies a top-down packed source into a
bottom-up destination with clipping. Destination row `dy + r` is stored at
`height - 1 - (dy + r)`. Source and destination are clipped together so the
corresponding source pixels are copied.

### 8.2 RGB555 conversion

```text
r8 = (v & 31) << 3 | (v & 31) >> 2
g8 = ((v >> 5) & 31) << 3 | ((v >> 5) & 31) >> 2
b8 = ((v >> 10) & 31) << 3 | ((v >> 10) & 31) >> 2
pixel = r8 << 16 | g8 << 8 | b8
```

The reverse packing uses the high 5 bits of each channel. Bit 15 is masked off
on input.

### 8.3 JPEG atlas

The JPEG is decoded to a top-down RGB atlas. Tiles are addressed bottom-up:

- tile size is the initialization tile size (default 16), changed by command 48;
- for each auxiliary rectangle, destination x is the outer loop and
  destination y is the inner loop, stepping by tile size;
- the source cursor starts at `column = 0, sy = 0` and walks **down one atlas
  column**, then moves to the next column and resets `sy` to 0;
- tile width/height are clipped to the rectangle edges;
- source row `sy + row` maps to atlas row `atlas.height - 1 - sy - row`.

Bounds are checked against the atlas and the screen. A malformed tile rejects
the frame.

## 9. Zstandard screen stream

Part 2 is a zstd stream compressed with the UltraViewer-trained dictionary
`8bit.dat` (112,640 bytes). The dictionary is required: a stream that references
it cannot be decoded without it.

Reference decoder:

- streaming decompression (`ZSTD_decompressStream`);
- a fresh session reset before every frame, while keeping the loaded dictionary;
- maximum window log 27;
- output limit 128 MiB per frame;
- supports concatenated frames;
- fails if the total output does not equal the expected pixel byte count.

The dictionary is an UltraViewer-supplied data asset and is **not** MIT/GPL
licensed by the MacViewer project. The plugin repository does not ship it by
default. The build/PKGBUILD can install a copy from the local MacViewer tree;
the runtime path is configurable (see the README).

## 10. Input encoding

### 10.1 Mouse

ASCII payload, one of:

```text
8|||512|||flags|||x|||y                 # WM_MOUSEMOVE
8|||513|||flags|||packed                # WM_LBUTTONDOWN
8|||514|||flags|||packed                # WM_LBUTTONUP
8|||516|||flags|||packed                # WM_RBUTTONDOWN
8|||517|||flags|||packed                # WM_RBUTTONUP
8|||519|||flags|||packed                # WM_MBUTTONDOWN
8|||520|||flags|||packed                # WM_MBUTTONUP
8|||522|||wParam|||packed               # WM_MOUSEWHEEL
```

- `flags` is a Windows mouse-key state: left `0x01`, right `0x02`, shift
  `0x04`, control `0x08`, middle `0x10`.
- `packed = (x & 0xffff) | ((y & 0xffff) << 16)` as a signed 32-bit integer.
- `wParam = ((wheel_delta & 0xffff) << 16) | flags`; one wheel notch is ±120,
  positive = up.
- Coordinates are in virtual-desktop pixels, including the initialization
  origin.

### 10.2 Keyboard (binary, host >= 6.2.13)

Exactly 11 bytes:

```text
1E 7C 7C 7C kind vk scan_le16 ch_le16 caps
```

- `kind`: 1 = key down, 2 = key up, 3 = system key down, 4 = system key up.
  Use system kinds while Alt is held (or for Alt itself).
- `vk`: Windows virtual-key code.
- `scan`: set-1 scan code, with `0xE0` in the high byte for extended keys.
- `ch`: UTF-16 code unit for printable ASCII text, or 0 for shortcuts/controls.
  Control characters: Return 13, Tab 9, Backspace 8, Escape 27.
- `caps`: 1 when Caps Lock is on.

Keys outside the US table can be typed as `VK_PACKET` (`0xE7`) down/up pairs,
with the UTF-16 code unit in `ch` and scan 0.

The reference mapping for letters/digits/punctuation uses the US set-1 table;
function/navigation/numpad keys and all modifier sides are included. See
`LinuxViewer/src/input.rs` for the table used to build the plugin's table.

### 10.3 Pre-6.2.13 hosts

Binary keyboard input is not supported by the host. The reference client drops
keyboard input when the partner version is below 6.2.13. Mouse input still
works.

## 11. Heartbeat and timeouts

| Timer | Action |
|-------|--------|
| 10 s | relay `ping` (system envelope, no flag) while relay is up |
| 10 s | peer ASCII `20` while active |
| 60 s | command keepalive `3DES("2")` |
| 20 s | login must be accepted (command 0) or fail |
| 3 s | TCP connect budget per resolved address |
| 30 s | write timeout |

The host expects a screen ack (`18`) after each frame before sending more.
This is used as backpressure. Send it even if decoding failed, then recover with
a refresh.

## 12. Limits and error handling

- Frame: 16 MiB.
- Multipart: 4096 parts, 64 MiB.
- Screen: dimension <= 16384, pixels <= 32,000,000, decompressed bytes
  <= 128 MiB.
- Cache: 1000 entries, 64 MiB.
- JPEG: must start with `FF D8`; atlas dimensions <= 65535; pixels bounded.
- Rectangle width/height must be positive; signed coordinates are allowed but
  clipped.
- A frame with a missing cache entry, bad sequence number, bad zstd output
  length, malformed part length or invalid JPEG is rejected atomically.
- Do not trust part lengths, rectangle counts or the frame size. All arithmetic
  must be overflow-checked.

## 13. Status codes and user-visible failures

| Condition | Message used by the reference client |
|-----------|--------------------------------------|
| registration timeout | UltraViewer did not answer the registration |
| lookup status 2 | UltraViewer rejected the partner password |
| lookup status 0 | The partner ID is not online |
| other lookup status | UltraViewer declined the partner lookup (code N) |
| no command-0 within 20 s | The partner did not accept the session in time |
| relay TLS invalid | The relay assigned to this session has an invalid security certificate |
| command 39 | The partner computer blocked this connection |
| commands 27/41/54 | The remote computer disconnected |
| decode failure x2 | The remote screen could not be decoded |

## 14. Test vectors

Command cipher (OpenSSL-compatible):

- password `fixture-⌘`
- plaintext `8:123456789:synthetic-proof`
- ciphertext hex
  `b63b9797064974fa31181fef729c755558492379dcb44c69e3fe4c6d0e29836c`

Server cipher:

- plaintext `8:fixture`
- ciphertext hex `c842551785856d0b8f866afed8dcd8b1`

Relay envelopes:

- `forward(192.0.2.10, 12345, flag=0, payload=AA BB)` =
  `00 00 0A 02 C0 39 30 AA BB`
- `system("ping", with_flag=false)` = `01 70 69 6E 67`
- `system(3DES("2"), with_flag=true)` =
  `01 00 2D 93 29 71 DD 9D CB 2D`

Login proofs (password `test-pass`, nonce `12345678`):

- field 2 = `c10f3394b41ec5b86060e0285e0dd306`
- field 13 = `85347ba7a403ffed52d311b7c5b6b549`
- display name `Mac: Viewer!` becomes `Mac Viewer`

## 15. Source map

The corresponding reference implementations are:

- registration: `MacViewer/Sources/UltraViewerProtocol/ClientRegistration.swift`,
  `LinuxViewer/src/messages.rs`
- lookup: `PeerLookup.swift`, `LinuxViewer/src/messages.rs`,
  `LinuxViewer/src/command.rs`
- login: `PartnerLoginMessage.swift`, `LinuxViewer/src/messages.rs`
- envelopes/cipher: `RelayEnvelope.swift`, `LegacyCommandCipher.swift`,
  `LinuxViewer/src/wire.rs`
- TLS/HTTP: `FramedTCPTransport.swift`, `RelayTrustAnchors.swift`,
  `LinuxViewer/src/tls.rs`
- screen init/frame: `ModernScreenMessage.swift`,
  `LinuxViewer/src/screen/{mod,frame}.rs`
- renderer: `ModernScreenRenderer.swift`,
  `LinuxViewer/src/screen/renderer.rs`
- zstd: `ZstdScreenDecoder.swift`, `LinuxViewer/src/screen/zstd.rs`
- input: `MouseMessage.swift`, `KeyboardMessage.swift`,
  `LinuxViewer/src/input.rs`
- session flow: `UltraViewerRelaySession.swift`,
  `LinuxViewer/src/client.rs`
