# Remmina protocol plugin research

This document records how a native Remmina protocol plugin is structured, using
the Remmina tree in this workspace and the installed Remmina 1.4.43 development
headers. It is the design basis for `remmina-plugin-ultraviewer`.

## 1. What a protocol plugin is

Remmina loads protocol plugins as shared objects (`*.so`) from its plugin
directory (normally `/usr/lib/remmina/plugins`). Each plugin exports one symbol:

```c
G_MODULE_EXPORT gboolean
remmina_plugin_entry(RemminaPluginService *service);
```

Remmina calls this during startup. The plugin stores the service pointer and
registers one or more `RemminaPlugin` structures. For a remote desktop protocol
the structure is a `RemminaProtocolPlugin`.

A plugin is **not** linked against a Remmina library. It resolves every Remmina
operation through the `RemminaPluginService` function-pointer table passed to
`remmina_plugin_entry`. This keeps the ABI stable and means the plugin needs the
Remmina development header at build time but no `-lremmina`.

Relevant installed headers:

- `/usr/include/remmina/plugin.h` — plugin structs, service table, entry type.
- `/usr/include/remmina/types.h` — settings, features, scale modes.
- `/usr/include/remmina/remmina_trace_calls.h` — optional trace macros.

On the workspace source HEAD, the structures are append-only relative to
1.4.43. A plugin built against 1.4.43 headers works with newer Remmina as long
as it does not use the newer fields.

## 2. Plugin descriptor

`RemminaProtocolPlugin` (installed 1.4.43 layout):

| Field | Purpose |
|-------|---------|
| `type` | `REMMINA_PLUGIN_TYPE_PROTOCOL` |
| `name` | short protocol name, uppercase, used as the profile protocol |
| `description` | human-readable name |
| `domain` | gettext domain (may be `NULL`/empty) |
| `version` | plugin version string |
| `icon_name` | icon for normal connections |
| `icon_name_ssh` | icon for SSH-tunnelled connections |
| `basic_settings` | settings shown in the basic profile tab |
| `advanced_settings` | settings shown in the advanced tab |
| `ssh_setting` | SSH tunnel capability |
| `features` | feature array (scale, view-only, tools, …) |
| `init` | widget construction |
| `open_connection` | start a session |
| `close_connection` | stop a session |
| `query_feature` | enable/disable a feature |
| `call_feature` | react to a feature |
| `send_keystrokes` | special key sequence support |
| `get_plugin_screenshot` | optional screenshot callback |
| `map_event` / `unmap_event` | optional RCW lifecycle callbacks |

The `name` becomes the profile's `protocol=` value. It must be unique and is
conventionally uppercase (`VNC`, `RDP`, `ULTRAVIEWER`). Do not use a translated
string.

## 3. Entry point and registration

```c
static RemminaPluginService *remmina_plugin_service;

G_MODULE_EXPORT gboolean
remmina_plugin_entry(RemminaPluginService *service)
{
    remmina_plugin_service = service;
    return service->register_plugin((RemminaPlugin *)&my_protocol_plugin);
}
```

Notes:

- Store the pointer before registering; callbacks may run immediately.
- Return `FALSE` if registration fails.
- Do not call `gtk_init`; Remmina already owns GTK.
- A plugin may register multiple protocol descriptors (the VNC plugin registers
  VNC and VNCI). Each `register_plugin` call is independent.

## 4. Lifecycle

### 4.1 `init(RemminaProtocolWidget *gp)`

Called once when Remmina creates the protocol widget. Typical work:

1. Allocate a plugin-data struct and attach it with
   `g_object_set_data_full(G_OBJECT(gp), "plugin-data", data, free_fn)`.
2. Create a `GtkDrawingArea` (or a container with one).
3. Add it to the `RemminaProtocolWidget`, which is a `GtkContainer`.
4. `gtk_widget_show()` the drawing area.
5. Add event masks and connect signals:
   - `draw`
   - `motion-notify-event`
   - `button-press-event` / `button-release-event`
   - `scroll-event` (and `GDK_SMOOTH_SCROLL_MASK` if desired)
   - `key-press-event` / `key-release-event`
   - `focus-out-event`
6. `gtk_widget_set_can_focus(drawing_area, TRUE)`.
7. Initialize locks, queues and any pipe used to wake the session thread.

`init` runs on the GTK main thread. It must not block.

### 4.2 `open_connection(RemminaProtocolWidget *gp)`

Called when the user connects. Typical work:

1. Read settings from the `RemminaFile`:
   `remmina_plugin_service->protocol_plugin_get_file(gp)`.
2. Validate them. Report invalid settings with
   `protocol_plugin_set_error()` and return `FALSE`.
3. `protocol_plugin_register_hostkey(gp, drawing_area)` so Remmina's hostkey
   handling sees key events.
4. Start the protocol worker thread (or schedule an idle main-loop task).
5. Return `TRUE` immediately.

The actual connection can take seconds. Do not block the GTK main thread.
Remmina shows a spinner/status while `open_connection` is true and the
connection-opened signal has not fired.

### 4.3 `close_connection(RemminaProtocolWidget *gp)`

Called when the user disconnects or the window closes. Typical work:

1. Tell the worker to stop.
2. Wake it if it is blocked in `poll`/`select`.
3. Shut down sockets so blocking reads return.
4. Join the worker (or poll a `running` flag with a timeout, as VNC does).
5. Free surfaces and session buffers.
6. Call `protocol_plugin_signal_connection_closed(gp)`.

The VNC plugin's pattern is useful when a worker cannot be cancelled safely:
`close_connection` sets `connected = FALSE` and waits for the worker's `running`
flag in a 200 ms timeout callback before freeing state.

### 4.4 `query_feature` / `call_feature`

`query_feature` is called to decide whether a feature can be offered. Return
`TRUE` unless the protocol cannot support it (for example a VNC server extension
that is not negotiated).

`call_feature` is called when the user invokes a feature. Common cases:

- `REMMINA_PROTOCOL_FEATURE_TYPE_SCALE`: the profile's `scale` setting changed.
  Adjust the drawing area's size request and call
  `protocol_plugin_update_align(gp)`.
- `REMMINA_PROTOCOL_FEATURE_TYPE_UNFOCUS`: release held keys/buttons.
- `REMMINA_PROTOCOL_FEATURE_TYPE_VIEWONLY`: release input when enabling.
- `REMMINA_PROTOCOL_FEATURE_TYPE_TOOL`: run the tool (refresh, chat, …).

### 4.5 `send_keystrokes`

Called with an array of GDK keyvals (for example Ctrl+Alt+Del from the toolbar).
The simplest implementation re-injects key events into the drawing area:

```c
remmina_plugin_service->protocol_plugin_send_keys_signals(
    drawing_area, keystrokes, keylen, GDK_KEY_PRESS | GDK_KEY_RELEASE);
```

This reuses the normal key event encoder, so it follows the same mapping and
modifier rules as physical input.

## 5. Settings

`RemminaProtocolSetting` fields:

| Field | Meaning |
|-------|---------|
| `type` | setting widget type |
| `name` | key in the RemminaFile |
| `label` | visible label (translatable) |
| `compact` | compact layout hint |
| `opt1` | value list for SELECT/COMBO, or mDNS service for SERVER |
| `opt2` | tooltip / help text |
| `validator_data` | user data for validator |
| `validator` | validation callback |

Common types:

- `REMMINA_PROTOCOL_SETTING_TYPE_SERVER` — host/ID field.
- `REMMINA_PROTOCOL_SETTING_TYPE_PASSWORD` — secret field.
- `REMMINA_PROTOCOL_SETTING_TYPE_TEXT` — free text.
- `REMMINA_PROTOCOL_SETTING_TYPE_CHECK` — boolean.
- `REMMINA_PROTOCOL_SETTING_TYPE_SELECT` / `COMBO` — value list.
- `REMMINA_PROTOCOL_SETTING_TYPE_FILE` / `FOLDER`.
- `REMMINA_PROTOCOL_SETTING_TYPE_INT` / `DOUBLE`.
- `REMMINA_PROTOCOL_SETTING_TYPE_END` — array terminator.

The arrays must be static and terminated with a `TYPE_END` entry. `name` is the
key passed to `file_get_string()`, `file_get_int()`, etc. Password values are
stored by Remmina's secret plugin unless the user disables password storage.

The `SERVER` field is still just a string from the plugin's point of view. A
non-host string such as an UltraViewer ID is valid; the plugin must not call
`get_server_port()` on it unless the protocol really uses host:port.

## 6. Features

`RemminaProtocolFeature` entries:

```c
{ REMMINA_PROTOCOL_FEATURE_TYPE_SCALE, ID, NULL, NULL, NULL },
{ REMMINA_PROTOCOL_FEATURE_TYPE_VIEWONLY, ID,
      GINT_TO_POINTER(REMMINA_PROTOCOL_FEATURE_PREF_CHECK),
      "viewonly", N_("View only") },
{ REMMINA_PROTOCOL_FEATURE_TYPE_END, 0, NULL, NULL, NULL }
```

Common feature types:

- `SCALE` — fit remote screen to the window.
- `UNFOCUS` — notification that the widget lost focus.
- `VIEWONLY` — toolbar toggle backed by a profile setting.
- `TOOL` — menu item; `opt1` is the label and `opt2` an optional icon name.
- `PREF` — preference exposed in the plugin preferences.
- `DYNRESUPDATE`, `MULTIMON`, `GTKSOCKET` — protocol-specific extensions.

For a new plugin, start with `SCALE`, `UNFOCUS`, `VIEWONLY`, and one `TOOL`
refresh item unless the protocol has no refresh mechanism.

## 7. Drawing and scaling

The usual pattern is a `GtkDrawingArea` plus a Cairo image surface:

```c
surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
```

- `CAIRO_FORMAT_RGB24` is a 32-bit native-endian pixel with 8 unused high bits
  and R/G/B in the low 24 bits. Writing `0x00RRGGBB` as a `uint32_t` works on
  both little- and big-endian hosts.
- Protect the surface with a mutex shared by the worker and the draw callback.
- `cairo_surface_flush()` before writing pixels outside Cairo,
  `cairo_surface_mark_dirty()` after.
- In the `draw` callback, paint the surface. For scaled mode:

```c
GtkAllocation a;
gtk_widget_get_allocation(widget, &a);
cairo_scale(cr, (double)a.width / remote_width,
                (double)a.height / remote_height);
cairo_set_source_surface(cr, surface, 0, 0);
cairo_paint(cr);
```

- Call `protocol_plugin_set_width()` / `set_height()` with the remote size, and
  `protocol_plugin_desktop_resize()` when it changes.
- For non-scaled mode, set the drawing area's size request to the remote size.
  For scaled mode, request `-1, -1` and let the parent allocate.
- `protocol_plugin_update_align(gp)` tells Remmina to re-align the widget after
  a size or scale change.
- To avoid flooding the main loop, coalesce redraws with an atomic
  `draw_pending` flag and a single idle callback.

The VNC plugin's `on_draw` in `plugins/vnc/vnc_plugin.c` is a compact reference
for this pattern.

## 8. Input

Input events arrive on the GTK main thread. Keep protocol writes off the main
thread: encode the payload in the event handler, append it to a thread-safe
queue, and wake the worker through a pipe or `g_idle_add`. The worker owns the
socket and performs all writes.

Coordinate mapping:

```c
if (scale_mode != REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE) {
    gtk_widget_get_allocation(widget, &a);
    x = x * remote_width  / a.width;
    y = y * remote_height / a.height;
}
```

Then apply any protocol-specific virtual-desktop origin. Clamp coordinates to
the protocol's accepted range before packing them into 16-bit fields.

Modifier state comes from `GdkEventKey.state`. For mouse events, translate Shift
and Control into the protocol's button flags. Track pressed keys and buttons so
they can be released on `focus-out-event`, view-only toggle and disconnect.

Keyboard mapping choices:

- Map GDK keyvals to protocol key codes for physical keys, shortcuts and
  navigation.
- Send printable text separately (or as a protocol text field) so non-US
  layouts work.
- Use system-key variants while Alt is held.
- Drop keyboard input when the host advertises a version that cannot decode it.

`protocol_plugin_register_hostkey()` is required for Remmina's hostkey/grabbing
behaviour; call it in `open_connection`, not `init`.

## 9. Threading rules

- GTK and Remmina widget calls should run on the main thread. Use
  `g_idle_add()` to marshal worker events (size change, connection opened,
  error) back to it.
- `protocol_plugin_set_error()`, `protocol_plugin_signal_connection_opened()`
  and `protocol_plugin_signal_connection_closed()` are commonly called from
  worker callbacks in existing plugins, but marshalling them is safer.
- A worker blocked in `read()` must be woken for shutdown. A self-pipe plus
  `poll()` is portable and avoids `pthread_cancel`.
- If you must cancel a thread, use cleanup handlers and never hold a mutex
  across a cancellation point. The VNC plugin uses `pthread_cancel` and a
  `running` flag; a poll-based worker is simpler.
- Free the plugin data only after the worker has stopped. Attach it with
  `g_object_set_data_full` and make the free function idempotent.

## 10. Errors and status

- `protocol_plugin_set_error(gp, fmt, ...)` sets the connection error shown by
  Remmina. Call it before signalling closure on failure.
- `protocol_plugin_init_show_retry(gp)` can offer a retry panel.
- `protocol_plugin_signal_connection_opened(gp)` tells Remmina the session is
  usable. Call it after the first successful screen initialization, not when
  the TCP socket opens.
- `protocol_plugin_signal_connection_closed(gp)` tells Remmina to tear down the
  connection UI.
- `protocol_plugin_is_closed(gp)` lets a worker check whether the user has
  already closed the widget before posting an error.

## 11. Build and install

A plugin is a `MODULE` shared library:

```cmake
add_library(remmina-plugin-ultraviewer MODULE src/uv_plugin.c ...)
set_target_properties(remmina-plugin-ultraviewer PROPERTIES
    PREFIX "" OUTPUT_NAME "remmina-plugin-ultraviewer")
target_include_directories(... PRIVATE /usr/include/remmina)
target_link_libraries(... PkgConfig::GTK3 OpenSSL::SSL OpenSSL::Crypto
                          ZSTD::ZSTD JPEG::JPEG pthread)
install(TARGETS remmina-plugin-ultraviewer
        LIBRARY DESTINATION lib/remmina/plugins)
```

No `-lremmina` link is needed or wanted. The exported entry symbol must not be
hidden: do not compile with `-fvisibility=hidden` unless the symbol is marked
`G_MODULE_EXPORT`.

The file name does not have to match the plugin `name`, but keeping
`remmina-plugin-<name>.so` is conventional. Remmina scans the directory and
calls `remmina_plugin_entry` in every loadable module.

Installed directory on Arch/Remmina 1.4.43:
`/usr/lib/remmina/plugins/`. CMake's `CMAKE_INSTALL_LIBDIR` is `lib` on Arch.

## 12. Compatibility notes: installed 1.4.43 vs source HEAD

- The installed `RemminaProtocolPlugin` has 17 fields through
  `unmap_event`; source HEAD adds newer callbacks at the end.
- `RemminaPluginService` in 1.4.43 has the function pointers listed in
  `/usr/include/remmina/plugin.h`, ending at `add_network_state`.
- Newer fields are append-only; a plugin built against 1.4.43 can run on a
  newer Remmina because it never reads past the fields it knows. The reverse
  (built against HEAD, run on 1.4.43) is unsafe if newer fields are referenced.
- The plugin's `name` is the profile protocol key. Changing it breaks existing
  profiles.
- Remmina 1.4.x uses GTK3. Do not link GTK4.
- The service table includes both older `protocol_plugin_*` and newer
  `protocol_widget_*` spellings; use the older names for 1.4.x compatibility.

## 13. Checklist for a new protocol plugin

- [ ] Decide the uppercase protocol `name` and icon.
- [ ] Create the static `RemminaProtocolPlugin` descriptor.
- [ ] Terminate settings and feature arrays with `TYPE_END`.
- [ ] Allocate plugin data in `init`; build the drawing area.
- [ ] Add input event masks and connect signals.
- [ ] In `open_connection`, read/validate settings, register hostkey, start a
      worker, return quickly.
- [ ] Marshal remote size/connection/error events to the main thread.
- [ ] In `close_connection`, wake and join the worker, free surfaces, signal
      closed.
- [ ] Implement `query_feature`/`call_feature` for scale, unfocus, view-only
      and any tools.
- [ ] Implement `send_keystrokes` via `protocol_plugin_send_keys_signals`.
- [ ] Build as a `MODULE`, no Remmina link, install into the plugin directory.
- [ ] Test with `remmina -c profile.remmina` and with the GUI.

## 14. Mapping to the UltraViewer plugin

| Remmina concept | UltraViewer implementation |
|-----------------|----------------------------|
| Protocol name | `ULTRAVIEWER` |
| Server field | 9-digit partner ID |
| Password field | partner password |
| Username field | display name sent in login |
| Worker thread | registration, lookup, relay TLS, session loop |
| Drawing area | Cairo RGB24 surface, renderer output |
| Input queue | GTK thread → byte payloads → wake pipe → worker |
| Scale feature | Cairo scaling + coordinate mapping |
| View-only | input callbacks consult the profile setting |
| Refresh tool | re-login with clear-control/hash to request a full screen |
| Close | send ASCII `19`, close TLS, wake and join worker |

Protocol details (bytes, crypto, screen format) are in
`docs/ultraviewer-protocol.md`.
