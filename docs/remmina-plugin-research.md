# Remmina Protocol Plugin Research & Architecture Blueprint

This document details the complete architecture, API conventions, and implementation blueprint for building native Remmina protocol plugins, specifically tailored for the `remmina-plugin-ultraviewer` remote desktop plugin.

It is synthesized from:
1. The Remmina source tree in this workspace (latest upstream Gitlab HEAD).
2. Installed Arch Linux Remmina 1.4.43 development headers (`/usr/include/remmina/plugin.h`, `/usr/include/remmina/types.h`).
3. Analysis of production protocol plugins (`plugins/vnc`, `plugins/rdp`, `plugins/spice`).

---

## Table of Contents

1. [Remmina Plugin Architecture & ABI Model](#1-remmina-plugin-architecture--abi-model)
2. [Plugin Descriptor: `RemminaProtocolPlugin`](#2-plugin-descriptor-remminaprotocolplugin)
3. [Entry Point & Registration](#3-entry-point--registration)
4. [Protocol Widget Lifecycle](#4-protocol-widget-lifecycle)
   - [4.1 Initialization: `init()`](#41-initialization-init)
   - [4.2 Establishing Connection: `open_connection()`](#42-establishing-connection-open_connection)
   - [4.3 Teardown: `close_connection()`](#43-teardown-close_connection)
   - [4.4 Feature Query & Invocation: `query_feature()` / `call_feature()`](#44-feature-query--invocation-query_feature--call_feature)
   - [4.5 Keystroke Injection: `send_keystrokes()`](#45-keystroke-injection-send_keystrokes)
5. [Connection Profile Settings Configuration](#5-connection-profile-settings-configuration)
6. [Feature & Tools Integration](#6-feature--tools-integration)
7. [Subsystem Integrations](#7-subsystem-integrations)
   - [7.1 Framebuffer & Cairo Rendering Subsystem](#71-framebuffer--cairo-rendering-subsystem)
   - [7.2 Input Encoding & Event Subsystem](#72-input-encoding--event-subsystem)
   - [7.3 Remote Hardware Cursor Subsystem](#73-remote-hardware-cursor-subsystem)
   - [7.4 Clipboard Synchronization Subsystem](#74-clipboard-synchronization-subsystem)
   - [7.5 In-App Chat Window Subsystem](#75-in-app-chat-window-subsystem)
   - [7.6 Remote Tools & Quick Actions Subsystem](#76-remote-tools--quick-actions-subsystem)
8. [Threading Model & IPC Synchronization](#8-threading-model--ipc-synchronization)
9. [Error Reporting & Status Signaling](#9-error-reporting--status-signaling)
10. [Asset Management: `8bit.dat` Dictionary](#10-asset-management-8bitdat-dictionary)
11. [Standalone CMake Build System Blueprint](#11-standalone-cmake-build-system-blueprint)
12. [Arch Linux PKGBUILD Blueprint](#12-arch-linux-pkgbuild-blueprint)
13. [Source Code Module Blueprint for `remmina-plugin-ultraviewer`](#13-source-code-module-blueprint-for-remmina-plugin-ultraviewer)
14. [Compatibility Matrix: 1.4.43 vs Upstream HEAD](#14-compatibility-matrix-1443-vs-upstream-head)

---

## 1. Remmina Plugin Architecture & ABI Model

Remmina discovers and dynamically loads plugins at runtime from `/usr/lib/remmina/plugins/*.so` using GModule (`g_module_open`).

### Stable Function-Pointer ABI
Unlike traditional shared libraries, a Remmina protocol plugin is **NEVER linked directly against a Remmina library** (i.e. no `-lremmina`). Instead:
- Remmina passes a function-pointer table `RemminaPluginService*` into the plugin's exported entry function `remmina_plugin_entry()`.
- The plugin invokes all Remmina core services (widget management, profile access, chat windows, notifications) exclusively through this dispatch table.
- This decoupling ensures ABI stability across minor versions and eliminates circular symbol dependencies.

```
┌────────────────────────────────────────────────────────┐
│                   Remmina Core                         │
│                                                        │
│   Calls remmina_plugin_entry(&service) at startup      │
│   Dispatches UI events (draw, key, button, feature)    │
└───────────────────────┬────────────────────────────────┘
                        │
       GModule Load     │  RemminaPluginService* table
                        ▼
┌────────────────────────────────────────────────────────┐
│             remmina-plugin-ultraviewer.so              │
│                                                        │
│   • Registers RemminaProtocolPlugin descriptor         │
│   • Owns GTK DrawingArea and Cairo surface             │
│   • Manages background UltraViewer session thread      │
│   • Speaks UltraViewer protocol (TLS, Zstd, JPEG, 3DES)│
└────────────────────────────────────────────────────────┘
```

---

## 2. Plugin Descriptor: `RemminaProtocolPlugin`

A protocol plugin declares its identity, settings tabs, UI callbacks, and supported capabilities through a static `RemminaProtocolPlugin` structure:

```c
typedef struct _RemminaProtocolPlugin {
    RemminaPluginType type;                           // REMMINA_PLUGIN_TYPE_PROTOCOL
    const gchar *name;                                // Uppercase protocol identifier (e.g. "ULTRAVIEWER")
    const gchar *description;                         // Translatable user description
    const gchar *domain;                              // Gettext domain (or GETTEXT_PACKAGE)
    const gchar *version;                             // Plugin version string (e.g. "0.1.0")
    const gchar *icon_name;                           // Normal connection icon name
    const gchar *icon_name_ssh;                       // SSH-tunnelled connection icon name
    const RemminaProtocolSetting *basic_settings;     // Basic profile configuration tab
    const RemminaProtocolSetting *advanced_settings;  // Advanced profile configuration tab
    RemminaProtocolSSHSetting ssh_setting;            // SSH tunneling mode (usually NO_SSH)
    const RemminaProtocolFeature *features;           // Capabilities and toolbar tools array

    // Core Lifecycle Callbacks
    void     (*init)(RemminaProtocolWidget *gp);
    gboolean (*open_connection)(RemminaProtocolWidget *gp);
    gboolean (*close_connection)(RemminaProtocolWidget *gp);
    gboolean (*query_feature)(RemminaProtocolWidget *gp, const RemminaProtocolFeature *feature);
    void     (*call_feature)(RemminaProtocolWidget *gp, const RemminaProtocolFeature *feature);
    void     (*send_keystrokes)(RemminaProtocolWidget *gp, const guint *keystrokes, gint keylen);
    gboolean (*get_plugin_screenshot)(RemminaProtocolWidget *gp, RemminaPluginScreenshotData *data);
    gboolean (*map_event)(RemminaProtocolWidget *gp, GdkEvent *event);
    gboolean (*unmap_event)(RemminaProtocolWidget *gp, GdkEvent *event);
} RemminaProtocolPlugin;
```

> [!IMPORTANT]
> **Protocol Name Case Sensitivity**: The `name` field defines the key written to `.remmina` connection files (e.g. `protocol=ULTRAVIEWER`). It **must be strictly uppercase** and unique across all loaded plugins.

---

## 3. Entry Point & Registration

Every plugin shared module exports exactly one public symbol:

```c
#include <remmina/plugin.h>

static RemminaPluginService *remmina_plugin_service = NULL;

G_MODULE_EXPORT gboolean
remmina_plugin_entry(RemminaPluginService *service)
{
    remmina_plugin_service = service;
    return service->register_plugin((RemminaPlugin *)&remmina_plugin_ultraviewer);
}
```

- `service` must be stored before calling `register_plugin()`, as Remmina may invoke callbacks immediately.
- Returns `TRUE` on successful registration, `FALSE` on failure.

---

## 4. Protocol Widget Lifecycle

### 4.1 Initialization: `init()`
Invoked on the GTK main thread when Remmina instantiates a `RemminaProtocolWidget` container for a connection:

```c
static void remmina_plugin_ultraviewer_init(RemminaProtocolWidget *gp)
{
    RemminaPluginUltraviewerData *gpdata = g_new0(RemminaPluginUltraviewerData, 1);
    g_object_set_data_full(G_OBJECT(gp), "plugin-data", gpdata, (GDestroyNotify)remmina_plugin_ultraviewer_free);

    gpdata->gp = gp;
    gpdata->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(gpdata->drawing_area, TRUE);
    gtk_container_add(GTK_CONTAINER(gp), gpdata->drawing_area);
    gtk_widget_show(gpdata->drawing_area);

    // Event masks
    gtk_widget_add_events(gpdata->drawing_area,
        GDK_POINTER_MOTION_MASK |
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_SCROLL_MASK |
        GDK_SMOOTH_SCROLL_MASK |
        GDK_KEY_PRESS_MASK |
        GDK_KEY_RELEASE_MASK |
        GDK_FOCUS_CHANGE_MASK);

    // Connect GTK signals
    g_signal_connect(gpdata->drawing_area, "draw", G_CALLBACK(remmina_plugin_ultraviewer_on_draw), gp);
    g_signal_connect(gpdata->drawing_area, "motion-notify-event", G_CALLBACK(remmina_plugin_ultraviewer_on_motion), gp);
    g_signal_connect(gpdata->drawing_area, "button-press-event", G_CALLBACK(remmina_plugin_ultraviewer_on_button_press), gp);
    g_signal_connect(gpdata->drawing_area, "button-release-event", G_CALLBACK(remmina_plugin_ultraviewer_on_button_release), gp);
    g_signal_connect(gpdata->drawing_area, "scroll-event", G_CALLBACK(remmina_plugin_ultraviewer_on_scroll), gp);
    g_signal_connect(gpdata->drawing_area, "key-press-event", G_CALLBACK(remmina_plugin_ultraviewer_on_key_press), gp);
    g_signal_connect(gpdata->drawing_area, "key-release-event", G_CALLBACK(remmina_plugin_ultraviewer_on_key_release), gp);
    g_signal_connect(gpdata->drawing_area, "focus-out-event", G_CALLBACK(remmina_plugin_ultraviewer_on_focus_out), gp);

    // Concurrency primitives
    g_mutex_init(&gpdata->surface_mutex);
    g_mutex_init(&gpdata->queue_mutex);
    gpdata->input_queue = g_async_queue_new();

    // Interruptible self-pipe for poll()
    if (pipe(gpdata->wake_pipe) != 0) {
        g_warning("Failed to create wake pipe");
    }
}
```

### 4.2 Establishing Connection: `open_connection()`
Invoked when the user triggers a connection. **Must never block the GTK main thread**:

1. Retrieve profile settings via `remmina_plugin_service->protocol_plugin_get_file(gp)`.
2. Validate required parameters (Partner ID, Password). If invalid, post an error via `protocol_plugin_set_error(gp, ...)` and return `FALSE`.
3. Register the hostkey handler:
   ```c
   remmina_plugin_service->protocol_plugin_register_hostkey(gp, gpdata->drawing_area);
   ```
4. Spawn the background protocol worker thread `uv_worker_thread`.
5. Return `TRUE` immediately. Remmina displays a progress spinner until `protocol_plugin_signal_connection_opened()` is signaled.

### 4.3 Teardown: `close_connection()`
Invoked when the session terminates or the window is closed:

1. Set `gpdata->running = FALSE`.
2. Write a byte to `gpdata->wake_pipe[1]` to wake the worker thread from any blocking `poll()`/`select()`.
3. Shut down network sockets (`shutdown(fd, SHUT_RDWR)`) to force blocking reads to return.
4. Join the worker thread (`g_thread_join()`).
5. Disconnect clipboard and idle handlers.
6. Call `remmina_plugin_service->protocol_plugin_signal_connection_closed(gp)`.

### 4.4 Feature Query & Invocation: `query_feature()` / `call_feature()`
- `query_feature()` returns `TRUE` if a requested feature is active and supported.
- `call_feature()` executes the corresponding action when clicked on the toolbar or changed in preferences.

### 4.5 Keystroke Injection: `send_keystrokes()`
Reuses Remmina's standard key event synthesizer:
```c
remmina_plugin_service->protocol_plugin_send_keys_signals(
    gpdata->drawing_area, keystrokes, keylen, GDK_KEY_PRESS | GDK_KEY_RELEASE);
```

---

## 5. Connection Profile Settings Configuration

Profile options are defined using static arrays of `RemminaProtocolSetting` structures, terminated by `REMMINA_PROTOCOL_SETTING_TYPE_END`:

```c
static const RemminaProtocolSetting remmina_plugin_ultraviewer_basic_settings[] = {
    { REMMINA_PROTOCOL_SETTING_TYPE_SERVER,   "server",    N_("Partner ID"),    FALSE, NULL, N_("9-digit UltraViewer partner ID") },
    { REMMINA_PROTOCOL_SETTING_TYPE_PASSWORD, "password",  N_("Password"),      FALSE, NULL, N_("Remote partner password") },
    { REMMINA_PROTOCOL_SETTING_TYPE_TEXT,     "username",  N_("Display Name"),  FALSE, NULL, N_("Client display name shown to host") },
    { REMMINA_PROTOCOL_SETTING_TYPE_END,      NULL,        NULL,                FALSE, NULL, NULL }
};

static const RemminaProtocolSetting remmina_plugin_ultraviewer_advanced_settings[] = {
    { REMMINA_PROTOCOL_SETTING_TYPE_SELECT,   "quality",           N_("Quality Preset"),     FALSE, "speed;balanced;quality", NULL },
    { REMMINA_PROTOCOL_SETTING_TYPE_CHECK,    "showcursor",        N_("Show Remote Cursor"), FALSE, NULL, NULL },
    { REMMINA_PROTOCOL_SETTING_TYPE_CHECK,    "disableclipboard",  N_("Disable Clipboard"),  FALSE, NULL, NULL },
    { REMMINA_PROTOCOL_SETTING_TYPE_FILE,     "dictpath",          N_("Dictionary Path"),    FALSE, NULL, N_("Path to 8bit.dat (optional)") },
    { REMMINA_PROTOCOL_SETTING_TYPE_CHECK,    "insecure_tls",      N_("Ignore TLS Errors"),  FALSE, NULL, N_("For debugging only") },
    { REMMINA_PROTOCOL_SETTING_TYPE_END,      NULL,                NULL,                     FALSE, NULL, NULL }
};
```

---

## 6. Feature & Tools Integration

UltraViewer capabilities are registered using `RemminaProtocolFeature`:

```c
enum {
    UV_FEATURE_SCALE = 1,
    UV_FEATURE_VIEWONLY,
    UV_FEATURE_UNFOCUS,
    UV_FEATURE_TOOL_REFRESH,
    UV_FEATURE_TOOL_CHAT,
    UV_FEATURE_TOOL_SENDCTRLALTDEL,
    UV_FEATURE_TOOL_LOCK,
    UV_FEATURE_TOOL_REBOOT,
    UV_FEATURE_TOOL_WALLPAPER,
    UV_FEATURE_PREF_QUALITY
};

static const RemminaProtocolFeature remmina_plugin_ultraviewer_features[] = {
    // Toolbar Tools
    { REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,     UV_FEATURE_TOOL_REFRESH,        N_("Refresh Screen"),       "view-refresh", NULL },
    { REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,     UV_FEATURE_TOOL_CHAT,           N_("Chat…"),                "face-smile",   NULL },
    { REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,     UV_FEATURE_TOOL_SENDCTRLALTDEL, N_("Send Ctrl+Alt+Delete"), NULL,           NULL },
    { REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,     UV_FEATURE_TOOL_LOCK,           N_("Lock Workstation"),     NULL,           NULL },
    { REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,     UV_FEATURE_TOOL_REBOOT,         N_("Reboot Computer"),      NULL,           NULL },
    { REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,     UV_FEATURE_TOOL_WALLPAPER,      N_("Toggle Wallpaper"),     NULL,           NULL },

    // Standard Features
    { REMMINA_PROTOCOL_FEATURE_TYPE_SCALE,    UV_FEATURE_SCALE,               NULL,                       NULL,           NULL },
    { REMMINA_PROTOCOL_FEATURE_TYPE_VIEWONLY, UV_FEATURE_VIEWONLY,            GINT_TO_POINTER(REMMINA_PROTOCOL_FEATURE_PREF_CHECK), "viewonly", N_("View only") },
    { REMMINA_PROTOCOL_FEATURE_TYPE_UNFOCUS,  UV_FEATURE_UNFOCUS,             NULL,                       NULL,           NULL },

    // Preferences
    { REMMINA_PROTOCOL_FEATURE_TYPE_PREF,     UV_FEATURE_PREF_QUALITY,        GINT_TO_POINTER(REMMINA_PROTOCOL_FEATURE_PREF_RADIO), "quality", quality_options },

    { REMMINA_PROTOCOL_FEATURE_TYPE_END,      0,                              NULL,                       NULL,           NULL }
};
```

---

## 7. Subsystem Integrations

### 7.1 Framebuffer & Cairo Rendering Subsystem

#### Surface Lifecycle & Thread Safety
The remote desktop is rendered into a Cairo image surface with format `CAIRO_FORMAT_RGB24`:
- The background worker thread holds `surface_mutex` while updating the surface pixels from decompressed Zstd and JPEG streams.
- The GTK `draw` signal acquires `surface_mutex` and renders the surface to the screen.

```c
static gboolean remmina_plugin_ultraviewer_on_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    RemminaProtocolWidget *gp = REMMINA_PROTOCOL_WIDGET(user_data);
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);

    g_mutex_lock(&gpdata->surface_mutex);
    if (gpdata->surface) {
        RemminaScaleMode scale_mode = remmina_plugin_service->remmina_protocol_widget_get_current_scale_mode(gp);
        if (scale_mode != REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE) {
            GtkAllocation a;
            gtk_widget_get_allocation(widget, &a);
            cairo_scale(cr, (double)a.width / gpdata->remote_width,
                            (double)a.height / gpdata->remote_height);
        }
        cairo_set_source_surface(cr, gpdata->surface, 0, 0);
        cairo_paint(cr);
    }
    g_mutex_unlock(&gpdata->surface_mutex);

    return FALSE;
}
```

#### Coalesced Redraw Marshalling
To prevent saturating the GTK main loop when frames arrive at 30–60 FPS, damage notifications are coalesced via an atomic flag:

```c
static gboolean uv_trigger_redraw_idle(gpointer user_data)
{
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    g_atomic_int_set(&gpdata->redraw_pending, 0);
    gtk_widget_queue_draw(gpdata->drawing_area);
    return G_SOURCE_REMOVE;
}

void uv_notify_frame_ready(RemminaPluginUltraviewerData *gpdata)
{
    if (g_atomic_int_compare_and_exchange(&gpdata->redraw_pending, 0, 1)) {
        g_idle_add(uv_trigger_redraw_idle, gpdata);
    }
}
```

---

### 7.2 Input Encoding & Event Subsystem

#### Coordinate Scaling
When scaling is active, coordinates must be transformed before applying virtual desktop offsets:

```c
static void uv_transform_coords(RemminaPluginUltraviewerData *gpdata, double widget_x, double widget_y, int *out_x, int *out_y)
{
    RemminaScaleMode scale_mode = remmina_plugin_service->remmina_protocol_widget_get_current_scale_mode(gpdata->gp);
    if (scale_mode != REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE) {
        GtkAllocation a;
        gtk_widget_get_allocation(gpdata->drawing_area, &a);
        widget_x = widget_x * gpdata->remote_width / a.width;
        widget_y = widget_y * gpdata->remote_height / a.height;
    }
    *out_x = (int)widget_x + gpdata->virtual_origin_x;
    *out_y = (int)widget_y + gpdata->virtual_origin_y;
}
```

#### GDK Keyval to Windows Virtual Key / Scan Code Mapping
The plugin maps X11/GDK key values (`GdkEventKey.keyval`) into:
1. Windows Virtual Key (VK) codes (`VK_LSHIFT`, `VK_RETURN`, `VK_CONTROL`, etc.).
2. Windows Hardware Scan Codes (Set 1).
3. Extended key indicator (`0xE0` high byte).
4. UTF-16 character codes for printable text.

For non-US layouts and complex IME characters, arbitrary Unicode code points are dispatched via `VK_PACKET` (`0xE7`).

---

### 7.3 Remote Hardware Cursor Subsystem

UltraViewer supports remote cursor shapes transferred as Windows `.cur` data (Command 4) and referenced by 32-bit hash (Command 3).

```
Host sends Command 4 (CUR data)
       │
       ▼
Decompress Zstd (no dict)
       │
       ▼
Parse Windows ICONDIR & DIB
       │
       ▼
Create Cairo Surface (with alpha mask)
       │
       ▼
g_idle_add() ──────► gdk_cursor_new_from_surface(display, surface, xhot, yhot)
                     gdk_window_set_cursor(gtk_widget_get_window(drawing_area), cursor)
```

- **Local Dot Cursor**: When "Show Remote Cursor" is enabled, the client hides the local mouse pointer (or sets a 3×3 dot cursor) while hovering over the remote canvas to avoid cursor ghosting.

---

### 7.4 Clipboard Synchronization Subsystem

Bidirectional clipboard integration connects to `GtkClipboard` (`GDK_SELECTION_CLIPBOARD`):

```c
static void uv_on_clipboard_owner_change(GtkClipboard *clipboard, GdkEvent *event, gpointer user_data)
{
    RemminaProtocolWidget *gp = REMMINA_PROTOCOL_WIDGET(user_data);
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);

    if (gpdata->view_only || gpdata->disable_clipboard) return;

    // Check loopback timer: suppress event if within 1000ms of remote update
    GDateTime *now = g_date_time_new_now_utc();
    gint64 diff = g_date_time_difference(now, gpdata->last_remote_clipboard_time) / 1000; // ms
    g_date_time_unref(now);

    if (diff < 1000) return;

    gtk_clipboard_request_text(clipboard, uv_on_clipboard_text_received, gpdata);
}
```

- When remote text arrives via Command 33/5:
  1. Record timestamp: `gpdata->last_remote_clipboard_time = g_date_time_new_now_utc()`.
  2. Write to local clipboard: `gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, len)`.

---

### 7.5 In-App Chat Window Subsystem

Remmina provides native chat window support directly in `RemminaPluginService`:

```c
// Open the chat window
static void uv_open_chat_dialog(RemminaProtocolWidget *gp)
{
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    remmina_plugin_service->protocol_plugin_chat_open(
        gp,
        gpdata->partner_display_name ? gpdata->partner_display_name : "UltraViewer Partner",
        uv_chat_on_send,
        uv_chat_on_destroy
    );
}

// User clicked 'Send' in the Remmina chat window
static void uv_chat_on_send(RemminaProtocolWidget *gp, const gchar *text)
{
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    // Queue Command 12 (UTF-16LE "12|||<text>") to worker thread
    uv_queue_chat_message(gpdata, text);
}

// Remote chat message arrived from host
void uv_handle_remote_chat(RemminaPluginUltraviewerData *gpdata, const gchar *text)
{
    // Deliver message directly to Remmina chat window
    remmina_plugin_service->protocol_plugin_chat_receive(gpdata->gp, text);
}
```

---

### 7.6 Remote Tools & Quick Actions Subsystem

The Tools menu dispatches administrative actions:

```c
static void remmina_plugin_ultraviewer_call_feature(RemminaProtocolWidget *gp, const RemminaProtocolFeature *feature)
{
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);

    switch (feature->id) {
    case UV_FEATURE_TOOL_REFRESH:
        uv_queue_session_refresh(gpdata);
        break;
    case UV_FEATURE_TOOL_CHAT:
        uv_open_chat_dialog(gp);
        break;
    case UV_FEATURE_TOOL_SENDCTRLALTDEL:
        uv_queue_unicode_action(gpdata, "22");
        break;
    case UV_FEATURE_TOOL_LOCK:
        uv_queue_unicode_action(gpdata, "53");
        break;
    case UV_FEATURE_TOOL_REBOOT:
        uv_queue_unicode_action(gpdata, "40");
        break;
    case UV_FEATURE_TOOL_WALLPAPER:
        gpdata->wallpaper_visible = !gpdata->wallpaper_visible;
        uv_queue_unicode_action(gpdata, gpdata->wallpaper_visible ? "23|||-1" : "23|||0");
        break;
    }
}
```

---

## 8. Threading Model & IPC Synchronization

The plugin strictly separates GTK event handling from network protocol I/O:

```
[ GTK Main Thread ]                              [ UltraViewer Worker Thread ]
  • User inputs (mouse, keys)                      • Open TLS command socket
  • Clipboard change signals                       • Register (3) & Lookup (8)
  • Toolbar actions                                • Open TLS relay socket
        │                                          • Send login message
        ▼                                                │
   g_async_queue_push()                                  ▼
        │                                         poll([relay_fd, wake_pipe])
        │ (wake byte on pipe)                            │
        └───────────────────────────────────────────────►│
                                                         • Read frames & decode
                                                         • Decompress Zstd pixels
                                                         • Blit to surface (under mutex)
                                                                 │
                                                                 ▼
                                                  g_idle_add(uv_notify_frame_ready)
```

### Clean Shutdown Sequence
To avoid thread deadlocks or crashes on close:
1. `close_connection()` sets `gpdata->running = FALSE`.
2. Writes `0x01` to `wake_pipe[1]`.
3. Calls `shutdown(relay_fd, SHUT_RDWR)` and `shutdown(cmd_fd, SHUT_RDWR)`.
4. The worker thread breaks out of `poll()`, closes sockets, frees local decoder contexts, and exits.
5. `g_thread_join()` returns safely.

---

## 9. Error Reporting & Status Signaling

- `protocol_plugin_set_error(gp, fmt, ...)` displays error banners in Remmina.
- `protocol_plugin_signal_connection_opened(gp)` transitions Remmina UI to active connected state (dismissing connect spinner).
- `protocol_plugin_signal_connection_closed(gp)` signals session completion and allows Remmina to tear down the tab/window.

---

## 10. Asset Management: `8bit.dat` Dictionary

### Search Path Priority
At session initialization, the decompressor searches for `8bit.dat` in the following order:
1. Path specified in profile setting `dictpath` (if non-empty).
2. Remmina shared application directory: `/usr/share/remmina/ultraviewer/8bit.dat`.
3. Plugin library directory: `/usr/lib/remmina/plugins/8bit.dat`.
4. User configuration directory: `~/.config/remmina/ultraviewer/8bit.dat`.
5. Local workspace fallback (for development builds).

If missing, the plugin reports:
`"UltraViewer dictionary (8bit.dat) not found. Run scripts/extract_assets.sh to extract it from the official installer."`

---

## 11. Standalone CMake Build System Blueprint

The plugin is structured as an independent CMake module built against the system Remmina headers:

```cmake
cmake_minimum_required(VERSION 3.16)
project(remmina-plugin-ultraviewer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(ZSTD REQUIRED libzstd)
pkg_check_modules(JPEG REQUIRED libjpeg)

find_path(REMMINA_INCLUDE_DIR remmina/plugin.h
    PATHS /usr/include /usr/local/include)
if(NOT REMMINA_INCLUDE_DIR)
    message(FATAL_ERROR "Remmina plugin headers not found (/usr/include/remmina/plugin.h)")
endif()

set(SOURCES
    src/uv_plugin.c
    src/uv_session.c
    src/uv_command.c
    src/uv_relay.c
    src/uv_screen.c
    src/uv_cursor.c
    src/uv_clipboard.c
    src/uv_chat.c
    src/uv_input.c
    src/uv_crypto.c
)

add_library(remmina-plugin-ultraviewer MODULE ${SOURCES})

set_target_properties(remmina-plugin-ultraviewer PROPERTIES
    PREFIX ""
    OUTPUT_NAME "remmina-plugin-ultraviewer"
)

target_include_directories(remmina-plugin-ultraviewer PRIVATE
    ${REMMINA_INCLUDE_DIR}
    ${GTK3_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${ZSTD_INCLUDE_DIRS}
    ${JPEG_INCLUDE_DIRS}
    include
)

target_link_libraries(remmina-plugin-ultraviewer PRIVATE
    ${GTK3_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${ZSTD_LIBRARIES}
    ${JPEG_LIBRARIES}
    m
    pthread
)

install(TARGETS remmina-plugin-ultraviewer
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/remmina/plugins"
)

install(FILES icons/scalable/apps/remmina-ultraviewer.svg
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
    OPTIONAL
)
```

---

## 12. Arch Linux PKGBUILD Blueprint

```bash
# Maintainer: Huy Minh <buingoc67@gmail.com>
pkgname=remmina-plugin-ultraviewer-git
pkgver=0.1.0.r0
pkgrel=1
pkgdesc="UltraViewer protocol plugin for Remmina"
arch=('x86_64')
url="https://github.com/hmtheboy154/remmina-plugin-ultraviewer"
license=('GPL-2.0-or-later')
depends=('remmina' 'gtk3' 'openssl' 'zstd' 'libjpeg-turbo')
makedepends=('git' 'cmake' 'pkgconf' 'p7zip')
provides=('remmina-plugin-ultraviewer')
conflicts=('remmina-plugin-ultraviewer')
source=("git+https://github.com/hmtheboy154/remmina-plugin-ultraviewer.git"
        "https://dl2.ultraviewer.net/UltraViewer_setup_6.6.133_vi.exe")
sha256sums=('SKIP'
            'SKIP')

prepare() {
    cd "${srcdir}"
    # Extract 8bit.dat from installer
    7z e -so UltraViewer_setup_6.6.133_vi.exe "8bit.dat" -r > 8bit.dat || true
    if [ ! -s 8bit.dat ]; then
        echo "Warning: 7z extraction failed, trying local fallback"
    fi
}

build() {
    cd "${srcdir}/remmina-plugin-ultraviewer"
    cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_INSTALL_LIBDIR=lib
    cmake --build build
}

package() {
    cd "${srcdir}/remmina-plugin-ultraviewer"
    DESTDIR="${pkgdir}" cmake --install build

    # Install dictionary asset if extracted
    if [ -f "${srcdir}/8bit.dat" ]; then
        install -Dm644 "${srcdir}/8bit.dat" "${pkgdir}/usr/share/remmina/ultraviewer/8bit.dat"
    fi
}
```

---

## 13. Source Code Module Blueprint for `remmina-plugin-ultraviewer`

```
remmina-plugin-ultraviewer/
├── CMakeLists.txt
├── LICENSE
├── README.md
├── PKGBUILD
├── docs/
│   ├── ultraviewer-protocol.md       # Complete wire-level protocol specification
│   └── remmina-plugin-research.md    # Remmina plugin architecture blueprint
├── scripts/
│   └── extract_assets.sh             # Asset downloader & extractor script
├── include/
│   ├── uv_plugin.h                   # Plugin descriptor and shared types
│   ├── uv_session.h                  # Worker thread and connection state
│   ├── uv_command.h                  # Command channel (Registration, Lookup, 3DES)
│   ├── uv_relay.h                    # Relay TLS channel, framing, envelopes
│   ├── uv_screen.h                   # Zstd decompressor, JPEG atlas, Cairo blitter
│   ├── uv_cursor.h                   # Windows CUR parser and GdkCursor cache
│   ├── uv_clipboard.h                # Clipboard sync and loopback timer
│   ├── uv_chat.h                     # In-session chat window bindings
│   ├── uv_input.h                    # Mouse/keyboard mapping and encoders
│   └── uv_crypto.h                   # 3DES, AES-CBC, MD5, SHA1 helpers
├── src/
│   ├── uv_plugin.c                   # Entry point and Remmina callbacks
│   ├── uv_session.c                  # Worker thread state machine
│   ├── uv_command.c                  # Registration, lookup, command keepalive
│   ├── uv_relay.c                    # Relay TLS handshake and envelope transport
│   ├── uv_screen.c                   # Frame rendering pipeline
│   ├── uv_cursor.c                   # Cursor shape decoding and GDK setup
│   ├── uv_clipboard.c                # GtkClipboard integration
│   ├── uv_chat.c                     # Remmina chat dialog hooks
│   ├── uv_input.c                    # Event translation (GDK -> Win32)
│   └── uv_crypto.c                   # OpenSSL wrapper functions
└── assets/
    └── .gitkeep                      # Target directory for 8bit.dat
```

---

## 14. Compatibility Matrix: 1.4.43 vs Upstream HEAD

- **Installed Remmina 1.4.43**: Uses the standard `RemminaProtocolPlugin` struct containing 17 fields up to `unmap_event`.
- **Upstream Gitlab HEAD**: Adds experimental callbacks at the end of `RemminaProtocolPlugin` (e.g. dynamic resolution renegotiation, hardware acceleration).
- **Binary Compatibility**: A plugin built against 1.4.43 headers is forward-compatible with newer Remmina versions because the struct layout is strictly append-only.
- **GTK3 Runtime**: Remmina 1.4.x relies strictly on GTK 3 (`libgtk-3.so`). Do not include or link GTK 4 headers.
