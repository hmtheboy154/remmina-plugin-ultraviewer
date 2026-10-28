/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UV_PLUGIN_H
#define UV_PLUGIN_H

#include "uv_types.h"
#include "uv_session.h"
#include <remmina/plugin.h>
#include <remmina/types.h>

#ifndef HAVE_REMMINA_PROTOCOL_FEATURE_TYPE_VIEWONLY
/* Remmina < 1.4.42 compatibility: VIEWONLY was REMMINA_PROTOCOL_FEATURE_TYPE_PREF */
#define REMMINA_PROTOCOL_FEATURE_TYPE_VIEWONLY REMMINA_PROTOCOL_FEATURE_TYPE_PREF
#endif

typedef struct {
    RemminaProtocolWidget *gp;
    GtkWidget *drawing_area;
    UvSession *session;

    int remote_width;
    int remote_height;

    bool view_only;
    bool show_cursor;
    bool disable_clipboard;
    bool wallpaper_visible;
    bool closed;
    bool user_cancelled;
    bool connection_opened;
    bool sound_muted;

    /* Coalesced redraw */
    volatile gint redraw_pending;

    /* Clipboard */
    gulong clipboard_handler;
    GDateTime *last_remote_clipboard_time;

    /* Cursor queueing */
    GdkCursor *queued_cursor;
    guint queued_cursor_handler;

    /* Remote cursor overlay */
    int16_t remote_cursor_x;
    int16_t remote_cursor_y;
    bool has_remote_cursor;
} RemminaPluginUltraviewerData;

#define GET_PLUGIN_DATA(gp) ((RemminaPluginUltraviewerData *)g_object_get_data(G_OBJECT(gp), "plugin-data"))

#endif /* UV_PLUGIN_H */
