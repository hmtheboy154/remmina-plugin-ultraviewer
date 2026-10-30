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

#include "uv_plugin.h"

#ifndef _
#define _(String) (String)
#endif
#ifndef N_
#define N_(String) (String)
#endif

static RemminaPluginService *remmina_plugin_service = NULL;

enum {
    UV_FEATURE_SCALE = 1,
    UV_FEATURE_VIEWONLY,
    UV_FEATURE_UNFOCUS,
    UV_FEATURE_TOOL_REFRESH,
    UV_FEATURE_TOOL_CHAT,
    UV_FEATURE_TOOL_SENDFILE,
    UV_FEATURE_TOOL_AUDIO,
    UV_FEATURE_TOOL_SENDCTRLALTDEL,
    UV_FEATURE_TOOL_LOCK,
    UV_FEATURE_TOOL_REBOOT,
    UV_FEATURE_TOOL_WALLPAPER,
    UV_FEATURE_PREF_QUALITY,
    UV_FEATURE_PREF_SHOWCURSOR
};

static const gchar *quality_list[] = {
    "30", N_("Speed"),
    "45", N_("Balanced"),
    "70", N_("High Quality"),
    NULL
};

static const RemminaProtocolSetting remmina_plugin_ultraviewer_basic_settings[] = {
    {
        .type = REMMINA_PROTOCOL_SETTING_TYPE_SERVER,
        .name = "server",
        .label = N_("Partner ID"),
        .compact = FALSE,
        .opt2 = (gpointer)N_("9-digit UltraViewer partner ID")
    },
    {
        .type = REMMINA_PROTOCOL_SETTING_TYPE_PASSWORD,
        .name = "password",
        .label = N_("Password"),
        .compact = FALSE,
        .opt2 = (gpointer)N_("Remote partner password")
    },
    {
        .type = REMMINA_PROTOCOL_SETTING_TYPE_TEXT,
        .name = "username",
        .label = N_("Display Name"),
        .compact = FALSE,
        .opt2 = (gpointer)N_("Display name shown on remote partner's screen")
    },
    { .type = REMMINA_PROTOCOL_SETTING_TYPE_END }
};

static const RemminaProtocolSetting remmina_plugin_ultraviewer_advanced_settings[] = {
    {
        .type = REMMINA_PROTOCOL_SETTING_TYPE_SELECT,
        .name = "quality",
        .label = N_("Quality Preset"),
        .compact = FALSE,
        .opt1 = (gpointer)quality_list
    },
    {
        .type = REMMINA_PROTOCOL_SETTING_TYPE_CHECK,
        .name = "showcursor",
        .label = N_("Show Remote Cursor"),
        .compact = FALSE
    },
    {
        .type = REMMINA_PROTOCOL_SETTING_TYPE_CHECK,
        .name = "disableclipboard",
        .label = N_("Disable Clipboard"),
        .compact = FALSE
    },
    {
        .type = REMMINA_PROTOCOL_SETTING_TYPE_CHECK,
        .name = "disablesound",
        .label = N_("Disable Remote Audio"),
        .compact = FALSE
    },
    {
        .type = REMMINA_PROTOCOL_SETTING_TYPE_FILE,
        .name = "dictpath",
        .label = N_("Dictionary Path"),
        .compact = FALSE,
        .opt2 = (gpointer)N_("Custom path to 8bit.dat dictionary")
    },
    {
        .type = REMMINA_PROTOCOL_SETTING_TYPE_CHECK,
        .name = "insecure_tls",
        .label = N_("Ignore TLS Errors"),
        .compact = FALSE,
        .opt2 = (gpointer)N_("Skip certificate validation (for testing only)")
    },
    { .type = REMMINA_PROTOCOL_SETTING_TYPE_END }
};

static const RemminaProtocolFeature remmina_plugin_ultraviewer_features[] = {
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,
        .id = UV_FEATURE_TOOL_REFRESH,
        .opt1 = (gpointer)N_("Refresh Screen"),
        .opt2 = (gpointer)"view-refresh"
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,
        .id = UV_FEATURE_TOOL_CHAT,
        .opt1 = (gpointer)N_("Chat…"),
        .opt2 = (gpointer)"face-smile"
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,
        .id = UV_FEATURE_TOOL_SENDFILE,
        .opt1 = (gpointer)N_("Send File…"),
        .opt2 = (gpointer)"document-send"
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,
        .id = UV_FEATURE_TOOL_AUDIO,
        .opt1 = (gpointer)N_("Toggle Audio"),
        .opt2 = (gpointer)"audio-volume-high"
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,
        .id = UV_FEATURE_TOOL_SENDCTRLALTDEL,
        .opt1 = (gpointer)N_("Send Ctrl+Alt+Delete")
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,
        .id = UV_FEATURE_TOOL_LOCK,
        .opt1 = (gpointer)N_("Lock Workstation")
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,
        .id = UV_FEATURE_TOOL_REBOOT,
        .opt1 = (gpointer)N_("Reboot Computer")
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_TOOL,
        .id = UV_FEATURE_TOOL_WALLPAPER,
        .opt1 = (gpointer)N_("Toggle Wallpaper")
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_SCALE,
        .id = UV_FEATURE_SCALE
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_VIEWONLY,
        .id = UV_FEATURE_VIEWONLY,
        .opt1 = GINT_TO_POINTER(REMMINA_PROTOCOL_FEATURE_PREF_CHECK),
        .opt2 = (gpointer)"viewonly",
        .opt3 = (gpointer)N_("View only")
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_UNFOCUS,
        .id = UV_FEATURE_UNFOCUS
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_PREF,
        .id = UV_FEATURE_PREF_QUALITY,
        .opt1 = GINT_TO_POINTER(REMMINA_PROTOCOL_FEATURE_PREF_RADIO),
        .opt2 = (gpointer)"quality",
        .opt3 = (gpointer)quality_list
    },
    {
        .type = REMMINA_PROTOCOL_FEATURE_TYPE_PREF,
        .id = UV_FEATURE_PREF_SHOWCURSOR,
        .opt1 = GINT_TO_POINTER(REMMINA_PROTOCOL_FEATURE_PREF_CHECK),
        .opt2 = (gpointer)"showcursor",
        .opt3 = (gpointer)N_("Show Remote Cursor")
    },
    { .type = REMMINA_PROTOCOL_FEATURE_TYPE_END }
};

static void uv_transform_coords(RemminaPluginUltraviewerData *gpdata, double widget_x, double widget_y, int *out_x, int *out_y)
{
    RemminaScaleMode scale_mode = remmina_plugin_service->remmina_protocol_widget_get_current_scale_mode(gpdata->gp);
    GtkAllocation a;
    gtk_widget_get_allocation(gpdata->drawing_area, &a);

    if (scale_mode != REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE && gpdata->remote_width > 0 && gpdata->remote_height > 0) {
        double sx = (double)a.width / (double)gpdata->remote_width;
        double sy = (double)a.height / (double)gpdata->remote_height;
        double scale = MIN(sx, sy);
        if (scale > 0.0) {
            double dw = (double)gpdata->remote_width * scale;
            double dh = (double)gpdata->remote_height * scale;
            double ox = ((double)a.width - dw) / 2.0;
            double oy = ((double)a.height - dh) / 2.0;
            widget_x = (widget_x - ox) / scale;
            widget_y = (widget_y - oy) / scale;
        }
    } else {
        double ox = 0.0, oy = 0.0;
        if (a.width > gpdata->remote_width) {
            ox = (double)(a.width - gpdata->remote_width) / 2.0;
        }
        if (a.height > gpdata->remote_height) {
            oy = (double)(a.height - gpdata->remote_height) / 2.0;
        }
        widget_x -= ox;
        widget_y -= oy;
    }

    if (widget_x < 0.0) widget_x = 0.0;
    if (gpdata->remote_width > 0 && widget_x >= (double)gpdata->remote_width) {
        widget_x = (double)gpdata->remote_width - 1.0;
    }
    if (widget_y < 0.0) widget_y = 0.0;
    if (gpdata->remote_height > 0 && widget_y >= (double)gpdata->remote_height) {
        widget_y = (double)gpdata->remote_height - 1.0;
    }

    int vx = (gpdata->session && gpdata->session->renderer) ? gpdata->session->renderer->virtual_origin_x : 0;
    int vy = (gpdata->session && gpdata->session->renderer) ? gpdata->session->renderer->virtual_origin_y : 0;

    *out_x = (int)widget_x + vx;
    *out_y = (int)widget_y + vy;
}

static uint16_t uv_get_mouse_flags(GdkModifierType state, guint button)
{
    uint16_t flags = 0;
    if (state & GDK_BUTTON1_MASK || button == 1) flags |= 0x01;
    if (state & GDK_BUTTON3_MASK || button == 3) flags |= 0x02;
    if (state & GDK_SHIFT_MASK)   flags |= 0x04;
    if (state & GDK_CONTROL_MASK) flags |= 0x08;
    if (state & GDK_BUTTON2_MASK || button == 2) flags |= 0x10;
    return flags;
}

static gboolean remmina_plugin_ultraviewer_on_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    (void)widget;
    RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    if (!gpdata || !gpdata->session || !gpdata->session->renderer) return FALSE;

    UvScreenRenderer *r = gpdata->session->renderer;
    g_mutex_lock(&r->mutex);
    if (r->cairo_surface) {
        RemminaScaleMode scale_mode = remmina_plugin_service->remmina_protocol_widget_get_current_scale_mode(gp);
        GtkAllocation a;
        gtk_widget_get_allocation(gpdata->drawing_area, &a);

        /* Fill background with solid black for clean letterbox/pillarbox borders */
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_paint(cr);

        if (scale_mode != REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE && gpdata->remote_width > 0 && gpdata->remote_height > 0) {
            double sx = (double)a.width / (double)gpdata->remote_width;
            double sy = (double)a.height / (double)gpdata->remote_height;
            double scale = MIN(sx, sy);
            double dw = (double)gpdata->remote_width * scale;
            double dh = (double)gpdata->remote_height * scale;
            double ox = ((double)a.width - dw) / 2.0;
            double oy = ((double)a.height - dh) / 2.0;

            cairo_save(cr);
            cairo_translate(cr, ox, oy);
            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, r->cairo_surface, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), (cairo_filter_t)remmina_plugin_service->pref_get_scale_quality());
            cairo_paint(cr);

            if (gpdata->show_cursor && gpdata->has_remote_cursor) {
                uv_cursor_draw_remote(gpdata->session->cursor_mgr, cr,
                                      gpdata->remote_cursor_x, gpdata->remote_cursor_y);
            }
            cairo_restore(cr);
        } else {
            double ox = 0.0, oy = 0.0;
            if (a.width > gpdata->remote_width) {
                ox = (double)(a.width - gpdata->remote_width) / 2.0;
            }
            if (a.height > gpdata->remote_height) {
                oy = (double)(a.height - gpdata->remote_height) / 2.0;
            }
            cairo_set_source_surface(cr, r->cairo_surface, ox, oy);
            cairo_paint(cr);

            if (gpdata->show_cursor && gpdata->has_remote_cursor) {
                uv_cursor_draw_remote(gpdata->session->cursor_mgr, cr,
                                      (int16_t)(gpdata->remote_cursor_x + ox),
                                      (int16_t)(gpdata->remote_cursor_y + oy));
            }
        }
    }
    g_mutex_unlock(&r->mutex);

    return TRUE;
}

static gboolean remmina_plugin_ultraviewer_on_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
    (void)widget;
    RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    if (!gpdata || gpdata->view_only || !gpdata->session) return FALSE;

    int x = 0, y = 0;
    uv_transform_coords(gpdata, event->x, event->y, &x, &y);
    uint16_t flags = uv_get_mouse_flags(event->state, 0);

    uint8_t *payload = NULL;
    size_t len = 0;
    if (uv_encode_mouse_move(x, y, flags, &payload, &len)) {
        uv_session_queue_outbound(gpdata->session, false, payload, len);
        g_free(payload);
    }

    return TRUE;
}

static gboolean remmina_plugin_ultraviewer_on_button(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)widget;
    RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    if (!gpdata || gpdata->view_only || !gpdata->session) return FALSE;

    gtk_widget_grab_focus(gpdata->drawing_area);

    int x = 0, y = 0;
    uv_transform_coords(gpdata, event->x, event->y, &x, &y);
    uint16_t flags = uv_get_mouse_flags(event->state, event->button);

    uint32_t msg = 0;
    if (event->type == GDK_BUTTON_PRESS) {
        if (event->button == 1) msg = UV_WM_LBUTTONDOWN;
        else if (event->button == 2) msg = UV_WM_MBUTTONDOWN;
        else if (event->button == 3) msg = UV_WM_RBUTTONDOWN;
    } else if (event->type == GDK_BUTTON_RELEASE) {
        if (event->button == 1) msg = UV_WM_LBUTTONUP;
        else if (event->button == 2) msg = UV_WM_MBUTTONUP;
        else if (event->button == 3) msg = UV_WM_RBUTTONUP;
    }

    if (msg != 0) {
        uint8_t *payload = NULL;
        size_t len = 0;
        if (uv_encode_mouse_button(msg, x, y, flags, &payload, &len)) {
            uv_session_queue_outbound(gpdata->session, false, payload, len);
            g_free(payload);
        }
    }

    return TRUE;
}

static gboolean remmina_plugin_ultraviewer_on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
    (void)widget;
    RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    if (!gpdata || gpdata->view_only || !gpdata->session) return FALSE;

    int x = 0, y = 0;
    uv_transform_coords(gpdata, event->x, event->y, &x, &y);
    uint16_t flags = uv_get_mouse_flags(event->state, 0);

    int16_t delta = 0;
    if (event->direction == GDK_SCROLL_UP) {
        delta = 120;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        delta = -120;
    } else if (event->direction == GDK_SCROLL_SMOOTH) {
        delta = (int16_t)(-event->delta_y * 120);
    }

    if (delta != 0) {
        uint8_t *payload = NULL;
        size_t len = 0;
        if (uv_encode_mouse_wheel(delta, x, y, flags, &payload, &len)) {
            uv_session_queue_outbound(gpdata->session, false, payload, len);
            g_free(payload);
        }
    }

    return TRUE;
}

static gboolean remmina_plugin_ultraviewer_on_key(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    (void)widget;
    RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    if (!gpdata || gpdata->view_only || !gpdata->session) return FALSE;

    bool is_press = (event->type == GDK_KEY_PRESS);
    uint8_t *payload = NULL;
    size_t len = 0;

    if (uv_encode_key(event->keyval, event->state, is_press, &payload, &len)) {
        uv_session_queue_outbound(gpdata->session, false, payload, len);
        g_free(payload);
        return TRUE;
    }

    return FALSE;
}

static void uv_on_clipboard_text_received(GtkClipboard *clipboard, const gchar *text, gpointer user_data)
{
    (void)clipboard;
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    if (!gpdata || gpdata->disable_clipboard || !gpdata->session || !text) return;

    uv_session_send_clipboard(gpdata->session, text);
}

static void uv_on_clipboard_owner_change(GtkClipboard *clipboard, GdkEvent *event, gpointer user_data)
{
    (void)event;
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    if (!gpdata || gpdata->disable_clipboard || !gpdata->session) return;

    if (gpdata->last_remote_clipboard_time) {
        GDateTime *now = g_date_time_new_now_utc();
        gint64 diff = g_date_time_difference(now, gpdata->last_remote_clipboard_time) / 1000;
        g_date_time_unref(now);
        if (diff < 1000) return; /* Suppress echo */
    }

    gtk_clipboard_request_text(clipboard, (GtkClipboardTextReceivedFunc)uv_on_clipboard_text_received, gpdata);
}

static void uv_chat_on_send(RemminaProtocolWidget *gp, const gchar *text)
{
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    if (gpdata && gpdata->session) {
        uv_session_send_chat(gpdata->session, text);
    }
}

static void uv_chat_on_destroy(RemminaProtocolWidget *gp)
{
    (void)gp;
}

/* Marshaled callbacks from worker thread to GTK main loop */
typedef struct {
    RemminaPluginUltraviewerData *gpdata;
    int width;
    int height;
    char *text;
    GdkCursor *cursor;
} MarshaledTask;

static gboolean on_connected_idle(gpointer user_data)
{
    MarshaledTask *task = (MarshaledTask *)user_data;
    RemminaPluginUltraviewerData *gpdata = task->gpdata;

    gpdata->remote_width = task->width;
    gpdata->remote_height = task->height;

    remmina_plugin_service->protocol_plugin_set_width(gpdata->gp, task->width);
    remmina_plugin_service->protocol_plugin_set_height(gpdata->gp, task->height);

    RemminaScaleMode scale_mode = remmina_plugin_service->remmina_protocol_widget_get_current_scale_mode(gpdata->gp);
    if (scale_mode != REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE) {
        gtk_widget_set_size_request(GTK_WIDGET(gpdata->drawing_area), -1, -1);
    } else {
        gtk_widget_set_size_request(GTK_WIDGET(gpdata->drawing_area), task->width, task->height);
    }
    gtk_widget_set_hexpand(GTK_WIDGET(gpdata->drawing_area), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(gpdata->drawing_area), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(gpdata->drawing_area), GTK_ALIGN_FILL);
    gtk_widget_set_valign(GTK_WIDGET(gpdata->drawing_area), GTK_ALIGN_FILL);

    if (!gpdata->connection_opened) {
        gpdata->connection_opened = true;
        remmina_plugin_service->protocol_plugin_update_align(gpdata->gp);
        remmina_plugin_service->protocol_plugin_desktop_resize(gpdata->gp);
        remmina_plugin_service->protocol_plugin_signal_connection_opened(gpdata->gp);
    } else {
        remmina_plugin_service->protocol_plugin_update_align(gpdata->gp);
        remmina_plugin_service->protocol_plugin_desktop_resize(gpdata->gp);
    }
    gtk_widget_queue_draw(gpdata->drawing_area);

    g_free(task);
    return G_SOURCE_REMOVE;
}

static void uv_session_connected_cb(UvSession *session, int width, int height, gpointer user_data)
{
    (void)session;
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    MarshaledTask *task = g_new0(MarshaledTask, 1);
    task->gpdata = gpdata;
    task->width = width;
    task->height = height;
    g_idle_add(on_connected_idle, task);
}

static gboolean on_frame_ready_idle(gpointer user_data)
{
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    g_atomic_int_set(&gpdata->redraw_pending, 0);
    gtk_widget_queue_draw(gpdata->drawing_area);
    return G_SOURCE_REMOVE;
}

static void uv_session_frame_ready_cb(UvSession *session, gpointer user_data)
{
    (void)session;
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    if (g_atomic_int_compare_and_exchange(&gpdata->redraw_pending, 0, 1)) {
        g_idle_add(on_frame_ready_idle, gpdata);
    }
}

static gboolean on_cursor_change_idle(gpointer user_data)
{
    MarshaledTask *task = (MarshaledTask *)user_data;
    RemminaPluginUltraviewerData *gpdata = task->gpdata;

    if (gpdata->queued_cursor) {
        g_object_unref(gpdata->queued_cursor);
        gpdata->queued_cursor = NULL;
    }
    if (task->cursor) {
        gpdata->queued_cursor = g_object_ref(task->cursor);
    }

    GdkWindow *win = gtk_widget_get_window(gpdata->drawing_area);
    if (win) {
        if (gpdata->show_cursor && task->cursor) {
            gdk_window_set_cursor(win, task->cursor);
        } else {
            gdk_window_set_cursor(win, NULL);
        }
    }

    if (task->cursor) g_object_unref(task->cursor);
    g_free(task);
    return G_SOURCE_REMOVE;
}

static void uv_session_cursor_change_cb(UvSession *session, GdkCursor *cursor, gpointer user_data)
{
    (void)session;
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    MarshaledTask *task = g_new0(MarshaledTask, 1);
    task->gpdata = gpdata;
    task->cursor = cursor ? g_object_ref(cursor) : NULL;
    g_idle_add(on_cursor_change_idle, task);
}

static void uv_session_cursor_position_cb(UvSession *session, int16_t x, int16_t y, gpointer user_data)
{
    (void)session;
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    if (gpdata) {
        if (gpdata->show_cursor) {
            gpdata->remote_cursor_x = x;
            gpdata->remote_cursor_y = y;
            gpdata->has_remote_cursor = true;
        } else {
            gpdata->has_remote_cursor = false;
        }
        if (g_atomic_int_compare_and_exchange(&gpdata->redraw_pending, 0, 1)) {
            g_idle_add(on_frame_ready_idle, gpdata);
        }
    }
}

static gboolean on_clipboard_idle(gpointer user_data)
{
    MarshaledTask *task = (MarshaledTask *)user_data;
    RemminaPluginUltraviewerData *gpdata = task->gpdata;

    if (gpdata->last_remote_clipboard_time) {
        g_date_time_unref(gpdata->last_remote_clipboard_time);
    }
    gpdata->last_remote_clipboard_time = g_date_time_new_now_utc();

    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), task->text, -1);

    g_free(task->text);
    g_free(task);
    return G_SOURCE_REMOVE;
}

static void uv_session_clipboard_cb(UvSession *session, const char *text, gpointer user_data)
{
    (void)session;
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    if (gpdata->disable_clipboard || !text) return;

    MarshaledTask *task = g_new0(MarshaledTask, 1);
    task->gpdata = gpdata;
    task->text = g_strdup(text);
    g_idle_add(on_clipboard_idle, task);
}

static gboolean on_chat_idle(gpointer user_data)
{
    MarshaledTask *task = (MarshaledTask *)user_data;
    remmina_plugin_service->protocol_plugin_chat_receive(task->gpdata->gp, task->text);
    g_free(task->text);
    g_free(task);
    return G_SOURCE_REMOVE;
}

static void uv_session_chat_cb(UvSession *session, const char *text, gpointer user_data)
{
    (void)session;
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    MarshaledTask *task = g_new0(MarshaledTask, 1);
    task->gpdata = gpdata;
    task->text = g_strdup(text);
    g_idle_add(on_chat_idle, task);
}

static gboolean on_closed_idle(gpointer user_data)
{
    MarshaledTask *task = (MarshaledTask *)user_data;
    RemminaPluginUltraviewerData *gpdata = task->gpdata;

    if (task->text && *task->text && strcmp(task->text, "Session ended") != 0 && !gpdata->user_cancelled) {
        remmina_plugin_service->protocol_plugin_set_error(gpdata->gp, "%s", task->text);
    }

    if (!gpdata->closed) {
        gpdata->closed = true;
        remmina_plugin_service->protocol_plugin_signal_connection_closed(gpdata->gp);
    }

    g_free(task->text);
    g_free(task);
    return G_SOURCE_REMOVE;
}

static void uv_session_closed_cb(UvSession *session, const char *error_msg, gpointer user_data)
{
    (void)session;
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    MarshaledTask *task = g_new0(MarshaledTask, 1);
    task->gpdata = gpdata;
    task->text = g_strdup(error_msg);
    g_idle_add(on_closed_idle, task);
}

static bool uv_session_auth_request_cb(UvSession *session, char **out_new_password, gpointer user_data)
{
    (void)session;
    RemminaPluginUltraviewerData *gpdata = (RemminaPluginUltraviewerData *)user_data;
    if (!gpdata || !gpdata->gp) return false;

    RemminaFile *file = remmina_plugin_service->protocol_plugin_get_file(gpdata->gp);
    gboolean disablepasswordstoring = remmina_plugin_service->file_get_int(file, "disablepasswordstoring", FALSE);

    /* Clear incorrect password in file */
    remmina_plugin_service->file_set_string(file, "password", NULL);

    gint ret = remmina_plugin_service->protocol_plugin_init_auth(
        gpdata->gp,
        (disablepasswordstoring ? 0 : REMMINA_MESSAGE_PANEL_FLAG_SAVEPASSWORD),
        _("UltraViewer Authentication Failed"),
        NULL,
        NULL,
        NULL,
        _("Password was incorrect. Please re-enter:")
    );

    if (ret != GTK_RESPONSE_OK) {
        return false;
    }

    char *new_pwd = remmina_plugin_service->protocol_plugin_init_get_password(gpdata->gp);
    if (!new_pwd || !*new_pwd) {
        g_free(new_pwd);
        return false;
    }

    gboolean save = remmina_plugin_service->protocol_plugin_init_get_savepassword(gpdata->gp);
    if (save) {
        remmina_plugin_service->file_set_string(file, "password", new_pwd);
    }

    *out_new_password = new_pwd;
    return true;
}

static void remmina_plugin_ultraviewer_free(RemminaPluginUltraviewerData *gpdata)
{
    if (!gpdata) return;

    if (gpdata->session) {
        uv_session_free(gpdata->session);
        gpdata->session = NULL;
    }

    if (gpdata->clipboard_handler) {
        g_signal_handler_disconnect(G_OBJECT(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)), gpdata->clipboard_handler);
        gpdata->clipboard_handler = 0;
    }

    if (gpdata->last_remote_clipboard_time) {
        g_date_time_unref(gpdata->last_remote_clipboard_time);
        gpdata->last_remote_clipboard_time = NULL;
    }

    if (gpdata->queued_cursor) {
        g_object_unref(gpdata->queued_cursor);
        gpdata->queued_cursor = NULL;
    }

    g_free(gpdata);
}

static void uv_on_drag_data_received(GtkWidget *widget, GdkDragContext *context,
                                     gint x, gint y, GtkSelectionData *data,
                                     guint info, guint time, gpointer user_data)
{
    (void)widget; (void)x; (void)y; (void)info;
    RemminaProtocolWidget *gp = (RemminaProtocolWidget *)user_data;
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    if (!gpdata || !gpdata->session) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    gchar **uris = gtk_selection_data_get_uris(data);
    if (uris) {
        for (int i = 0; uris[i]; i++) {
            char *filename = g_filename_from_uri(uris[i], NULL, NULL);
            if (filename) {
                uv_session_send_file(gpdata->session, filename);
                g_free(filename);
            }
        }
        g_strfreev(uris);
        gtk_drag_finish(context, TRUE, FALSE, time);
    } else {
        gtk_drag_finish(context, FALSE, FALSE, time);
    }
}

static void remmina_plugin_ultraviewer_init(RemminaProtocolWidget *gp)
{
    RemminaPluginUltraviewerData *gpdata = g_new0(RemminaPluginUltraviewerData, 1);
    gpdata->gp = gp;
    gpdata->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(gpdata->drawing_area, TRUE);
    gtk_widget_set_hexpand(gpdata->drawing_area, TRUE);
    gtk_widget_set_vexpand(gpdata->drawing_area, TRUE);
    gtk_container_add(GTK_CONTAINER(gp), gpdata->drawing_area);
    gtk_widget_show(gpdata->drawing_area);

    gtk_drag_dest_set(gpdata->drawing_area, GTK_DEST_DEFAULT_ALL, NULL, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(gpdata->drawing_area);
    g_signal_connect(gpdata->drawing_area, "drag-data-received", G_CALLBACK(uv_on_drag_data_received), gp);

    gtk_widget_add_events(gpdata->drawing_area,
        GDK_POINTER_MOTION_MASK |
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_SCROLL_MASK |
        GDK_SMOOTH_SCROLL_MASK |
        GDK_KEY_PRESS_MASK |
        GDK_KEY_RELEASE_MASK |
        GDK_FOCUS_CHANGE_MASK);

    g_signal_connect(gpdata->drawing_area, "draw", G_CALLBACK(remmina_plugin_ultraviewer_on_draw), gp);
    g_signal_connect(gpdata->drawing_area, "motion-notify-event", G_CALLBACK(remmina_plugin_ultraviewer_on_motion), gp);
    g_signal_connect(gpdata->drawing_area, "button-press-event", G_CALLBACK(remmina_plugin_ultraviewer_on_button), gp);
    g_signal_connect(gpdata->drawing_area, "button-release-event", G_CALLBACK(remmina_plugin_ultraviewer_on_button), gp);
    g_signal_connect(gpdata->drawing_area, "scroll-event", G_CALLBACK(remmina_plugin_ultraviewer_on_scroll), gp);
    g_signal_connect(gpdata->drawing_area, "key-press-event", G_CALLBACK(remmina_plugin_ultraviewer_on_key), gp);
    g_signal_connect(gpdata->drawing_area, "key-release-event", G_CALLBACK(remmina_plugin_ultraviewer_on_key), gp);

    g_object_set_data_full(G_OBJECT(gp), "plugin-data", gpdata, (GDestroyNotify)remmina_plugin_ultraviewer_free);
}

static gboolean remmina_plugin_ultraviewer_open_connection(RemminaProtocolWidget *gp)
{
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    RemminaFile *file = remmina_plugin_service->protocol_plugin_get_file(gp);

    const char *server = remmina_plugin_service->file_get_string(file, "server");
    const char *password = remmina_plugin_service->file_get_string(file, "password");
    const char *username = remmina_plugin_service->file_get_string(file, "username");
    const char *dictpath = remmina_plugin_service->file_get_string(file, "dictpath");
    int quality = remmina_plugin_service->file_get_int(file, "quality", 45);
    gboolean showcursor = remmina_plugin_service->file_get_int(file, "showcursor", FALSE);
    gboolean disableclipboard = remmina_plugin_service->file_get_int(file, "disableclipboard", FALSE);
    gboolean disablesound = remmina_plugin_service->file_get_int(file, "disablesound", FALSE);
    gboolean insecure_tls = remmina_plugin_service->file_get_int(file, "insecure_tls", FALSE);
    gboolean viewonly = remmina_plugin_service->file_get_int(file, "viewonly", FALSE);

    char clean_server[64];
    size_t sidx = 0;
    if (server) {
        for (const char *p = server; *p && sidx < sizeof(clean_server) - 1; p++) {
            if (!g_ascii_isspace(*p)) {
                clean_server[sidx++] = *p;
            }
        }
    }
    clean_server[sidx] = '\0';

    if (clean_server[0] == '\0') {
        remmina_plugin_service->protocol_plugin_set_error(gp, "%s", _("Partner ID is required"));
        return FALSE;
    }

    char *auth_pwd = NULL;
    if (!password || !*password) {
        gboolean disablepasswordstoring = remmina_plugin_service->file_get_int(file, "disablepasswordstoring", FALSE);
        gint ret = remmina_plugin_service->protocol_plugin_init_auth(
            gp,
            (disablepasswordstoring ? 0 : REMMINA_MESSAGE_PANEL_FLAG_SAVEPASSWORD),
            _("UltraViewer Authentication"),
            NULL,
            NULL,
            NULL,
            _("Enter UltraViewer Password")
        );
        if (ret != GTK_RESPONSE_OK) {
            return FALSE;
        }
        auth_pwd = remmina_plugin_service->protocol_plugin_init_get_password(gp);
        if (!auth_pwd || !*auth_pwd) {
            g_free(auth_pwd);
            remmina_plugin_service->protocol_plugin_set_error(gp, "%s", _("Password is required"));
            return FALSE;
        }
        gboolean save = remmina_plugin_service->protocol_plugin_init_get_savepassword(gp);
        if (save) {
            remmina_plugin_service->file_set_string(file, "password", auth_pwd);
        } else {
            remmina_plugin_service->file_set_string(file, "password", NULL);
        }
        password = auth_pwd;
    }

    int scale_val = remmina_plugin_service->file_get_int(file, "scale", -1);
    if (scale_val == -1) {
        remmina_plugin_service->file_set_int(file, "scale", REMMINA_PROTOCOL_WIDGET_SCALE_MODE_SCALED);
    }

    gpdata->view_only = viewonly;
    gpdata->show_cursor = showcursor;
    gpdata->disable_clipboard = disableclipboard;
    gpdata->sound_muted = disablesound;

    remmina_plugin_service->protocol_plugin_register_hostkey(gp, gpdata->drawing_area);

    if (!gpdata->disable_clipboard) {
        gpdata->clipboard_handler = g_signal_connect(
            G_OBJECT(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)),
            "owner-change",
            G_CALLBACK(uv_on_clipboard_owner_change),
            gpdata
        );
    }

    char default_user[64];
    if (username && *username && strcmp(username, "Remmina") != 0) {
        g_strlcpy(default_user, username, sizeof(default_user));
    } else {
        const char *host = g_get_host_name();
        if (host && *host) {
            snprintf(default_user, sizeof(default_user), "Remmina - %s", host);
        } else {
            snprintf(default_user, sizeof(default_user), "Remmina");
        }
    }

    gpdata->session = uv_session_new(clean_server, password, default_user, dictpath, quality, insecure_tls, gpdata);
    g_free(auth_pwd);
    gpdata->session->sound_enabled = !disablesound;
    gpdata->session->show_cursor = gpdata->show_cursor;

    gpdata->session->on_connected = uv_session_connected_cb;
    gpdata->session->on_frame_ready = uv_session_frame_ready_cb;
    gpdata->session->on_cursor_change = uv_session_cursor_change_cb;
    gpdata->session->on_cursor_position = uv_session_cursor_position_cb;
    gpdata->session->on_clipboard_received = uv_session_clipboard_cb;
    gpdata->session->on_chat_received = uv_session_chat_cb;
    gpdata->session->on_closed = uv_session_closed_cb;
    gpdata->session->on_auth_request = uv_session_auth_request_cb;

    return uv_session_start(gpdata->session);
}

static gboolean remmina_plugin_ultraviewer_close_connection(RemminaProtocolWidget *gp)
{
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    if (!gpdata) return FALSE;

    gpdata->user_cancelled = true;

    if (gpdata->clipboard_handler) {
        g_signal_handler_disconnect(G_OBJECT(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)), gpdata->clipboard_handler);
        gpdata->clipboard_handler = 0;
    }

    if (gpdata->session) {
        uv_session_stop(gpdata->session);
    }

    if (!gpdata->closed) {
        gpdata->closed = true;
        remmina_plugin_service->protocol_plugin_signal_connection_closed(gp);
    }
    return TRUE;
}

static gboolean remmina_plugin_ultraviewer_query_feature(RemminaProtocolWidget *gp, const RemminaProtocolFeature *feature)
{
    (void)gp;
    (void)feature;
    return TRUE;
}

static void remmina_plugin_ultraviewer_call_feature(RemminaProtocolWidget *gp, const RemminaProtocolFeature *feature)
{
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    if (!gpdata || !gpdata->session) return;

    switch (feature->id) {
    case UV_FEATURE_TOOL_REFRESH: {
        RemminaFile *file = remmina_plugin_service->protocol_plugin_get_file(gp);
        int qval = remmina_plugin_service->file_get_int(file, "quality", 45);
        if (gpdata->session) {
            gpdata->session->quality_preset = qval;
        }
        uv_session_queue_outbound(gpdata->session, false, (const uint8_t *)"32|||2", 6);
        char qbuf[32];
        g_snprintf(qbuf, sizeof(qbuf), "31|||%d", qval);
        uv_session_queue_outbound(gpdata->session, false, (const uint8_t *)qbuf, strlen(qbuf));
        uv_session_refresh_screen(gpdata->session);
        break;
    }
    case UV_FEATURE_TOOL_CHAT:
        remmina_plugin_service->protocol_plugin_chat_open(
            gp,
            gpdata->session->partner_lookup.partner_name[0] ? gpdata->session->partner_lookup.partner_name : "UltraViewer Partner",
            uv_chat_on_send,
            uv_chat_on_destroy
        );
        break;
    case UV_FEATURE_TOOL_SENDFILE: {
        GtkWidget *toplevel = gtk_widget_get_toplevel(gpdata->drawing_area);
        GtkWidget *dialog = gtk_file_chooser_dialog_new(
            _("Send File to Partner"),
            GTK_WINDOW(toplevel),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            _("_Cancel"), GTK_RESPONSE_CANCEL,
            _("_Send"), GTK_RESPONSE_ACCEPT,
            NULL
        );
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (filename) {
                uv_session_send_file(gpdata->session, filename);
                g_free(filename);
            }
        }
        gtk_widget_destroy(dialog);
        break;
    }
    case UV_FEATURE_TOOL_AUDIO: {
        gpdata->sound_muted = !gpdata->sound_muted;
        uv_session_toggle_sound(gpdata->session, !gpdata->sound_muted);
        break;
    }
    case UV_FEATURE_TOOL_SENDCTRLALTDEL:
        uv_session_send_action(gpdata->session, "22");
        break;
    case UV_FEATURE_TOOL_LOCK:
        uv_session_send_action(gpdata->session, "53");
        break;
    case UV_FEATURE_TOOL_REBOOT:
        uv_session_send_action(gpdata->session, "40");
        break;
    case UV_FEATURE_TOOL_WALLPAPER:
        gpdata->wallpaper_visible = !gpdata->wallpaper_visible;
        uv_session_send_action(gpdata->session, gpdata->wallpaper_visible ? "23|||-1" : "23|||0");
        break;
    case UV_FEATURE_SCALE: {
        RemminaScaleMode scale_mode = remmina_plugin_service->remmina_protocol_widget_get_current_scale_mode(gp);
        if (scale_mode != REMMINA_PROTOCOL_WIDGET_SCALE_MODE_NONE) {
            gtk_widget_set_size_request(GTK_WIDGET(gpdata->drawing_area), -1, -1);
        } else {
            int w = gpdata->remote_width > 0 ? gpdata->remote_width : remmina_plugin_service->protocol_plugin_get_width(gp);
            int h = gpdata->remote_height > 0 ? gpdata->remote_height : remmina_plugin_service->protocol_plugin_get_height(gp);
            gtk_widget_set_size_request(GTK_WIDGET(gpdata->drawing_area), w, h);
        }
        gtk_widget_set_hexpand(GTK_WIDGET(gpdata->drawing_area), TRUE);
        gtk_widget_set_vexpand(GTK_WIDGET(gpdata->drawing_area), TRUE);
        gtk_widget_set_halign(GTK_WIDGET(gpdata->drawing_area), GTK_ALIGN_FILL);
        gtk_widget_set_valign(GTK_WIDGET(gpdata->drawing_area), GTK_ALIGN_FILL);
        remmina_plugin_service->protocol_plugin_update_align(gp);
        remmina_plugin_service->protocol_plugin_desktop_resize(gp);
        gtk_widget_queue_draw(gpdata->drawing_area);
        break;
    }
    case UV_FEATURE_PREF_SHOWCURSOR: {
        RemminaFile *file = remmina_plugin_service->protocol_plugin_get_file(gp);
        gpdata->show_cursor = remmina_plugin_service->file_get_int(file, "showcursor", FALSE);
        if (gpdata->session) {
            gpdata->session->show_cursor = gpdata->show_cursor;
            const char *cur_cmd = gpdata->show_cursor ? "28|||1" : "28|||0";
            uv_session_queue_outbound(gpdata->session, false, (const uint8_t *)cur_cmd, strlen(cur_cmd));
        }
        GdkWindow *win = gtk_widget_get_window(gpdata->drawing_area);
        if (win) {
            if (gpdata->show_cursor && gpdata->queued_cursor) {
                gdk_window_set_cursor(win, gpdata->queued_cursor);
            } else {
                gdk_window_set_cursor(win, NULL);
            }
        }
        if (!gpdata->show_cursor) {
            gpdata->has_remote_cursor = false;
        }
        gtk_widget_queue_draw(gpdata->drawing_area);
        break;
    }
    case UV_FEATURE_VIEWONLY: {
        RemminaFile *file = remmina_plugin_service->protocol_plugin_get_file(gp);
        gpdata->view_only = remmina_plugin_service->file_get_int(file, "viewonly", FALSE);
        break;
    }
    case UV_FEATURE_PREF_QUALITY: {
        RemminaFile *file = remmina_plugin_service->protocol_plugin_get_file(gp);
        const char *qs = remmina_plugin_service->file_get_string(file, "quality");
        int q = 45;
        if (qs && *qs) {
            q = atoi(qs);
        } else {
            q = remmina_plugin_service->file_get_int(file, "quality", 45);
        }
        if (q <= 0) q = 45;
        if (gpdata->session) {
            gpdata->session->quality_preset = q;
        }

        uv_session_queue_outbound(gpdata->session, false, (const uint8_t *)"32|||2", 6);
        char q_cmd[32];
        snprintf(q_cmd, sizeof(q_cmd), "31|||%d", q);
        uv_session_queue_outbound(gpdata->session, false, (const uint8_t *)q_cmd, strlen(q_cmd));
        break;
    }
    default:
        break;
    }
}

static void remmina_plugin_ultraviewer_send_keystrokes(RemminaProtocolWidget *gp, const guint *keystrokes, gint keylen)
{
    RemminaPluginUltraviewerData *gpdata = GET_PLUGIN_DATA(gp);
    if (!gpdata) return;

    remmina_plugin_service->protocol_plugin_send_keys_signals(
        gpdata->drawing_area, keystrokes, keylen, GDK_KEY_PRESS | GDK_KEY_RELEASE);
}

static void find_and_inject_quick_connect(GtkWidget *widget, gpointer data)
{
    gboolean *found = (gboolean *)data;
    if (!widget || *found) return;

    if (GTK_IS_COMBO_BOX_TEXT(widget)) {
        const gchar *bname = gtk_buildable_get_name(GTK_BUILDABLE(widget));
        if (g_strcmp0(bname, "combo_quick_connect_protocol") == 0) {
            GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
            GtkTreeIter iter;
            gboolean exists = FALSE;
            if (model && gtk_tree_model_get_iter_first(model, &iter)) {
                do {
                    gchar *str = NULL;
                    gtk_tree_model_get(model, &iter, 0, &str, -1);
                    if (g_strcmp0(str, "ULTRAVIEWER") == 0) {
                        exists = TRUE;
                        g_free(str);
                        break;
                    }
                    g_free(str);
                } while (gtk_tree_model_iter_next(model, &iter));
            }
            if (!exists) {
                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(widget), "ULTRAVIEWER", "ULTRAVIEWER");
            }
            *found = TRUE;
            return;
        }
    }

    if (GTK_IS_CONTAINER(widget)) {
        gtk_container_forall(GTK_CONTAINER(widget), find_and_inject_quick_connect, data);
    }
}

static gboolean inject_quick_connect_timer(gpointer user_data)
{
    (void)user_data;
    GList *toplevels = gtk_window_list_toplevels();
    for (GList *l = toplevels; l; l = l->next) {
        GtkWidget *win = GTK_WIDGET(l->data);
        if (GTK_IS_WINDOW(win)) {
            /* Skip window if already inspected and injected */
            if (g_object_get_data(G_OBJECT(win), "uv-injected")) {
                continue;
            }
            gboolean found = FALSE;
            find_and_inject_quick_connect(win, &found);
            if (found) {
                /* Tag this GtkWindow so we don't re-traverse it while open */
                g_object_set_data(G_OBJECT(win), "uv-injected", GINT_TO_POINTER(1));
            }
        }
    }
    g_list_free(toplevels);
    return G_SOURCE_CONTINUE;
}

static RemminaProtocolPlugin remmina_plugin_ultraviewer = {
    .type = REMMINA_PLUGIN_TYPE_PROTOCOL,
    .name = "ULTRAVIEWER",
    .description = "UltraViewer - Remote Desktop Client Plugin",
    .domain = "remmina-plugin-ultraviewer",
    .version = "0.2.1",
    .icon_name = "remmina-ultraviewer",
    .icon_name_ssh = "remmina-ultraviewer-ssh",
    .basic_settings = remmina_plugin_ultraviewer_basic_settings,
    .advanced_settings = remmina_plugin_ultraviewer_advanced_settings,
    .ssh_setting = REMMINA_PROTOCOL_SSH_SETTING_NONE,
    .features = remmina_plugin_ultraviewer_features,
    .init = remmina_plugin_ultraviewer_init,
    .open_connection = remmina_plugin_ultraviewer_open_connection,
    .close_connection = remmina_plugin_ultraviewer_close_connection,
    .query_feature = remmina_plugin_ultraviewer_query_feature,
    .call_feature = remmina_plugin_ultraviewer_call_feature,
    .send_keystrokes = remmina_plugin_ultraviewer_send_keystrokes,
    .get_plugin_screenshot = NULL,
    .map_event = NULL,
    .unmap_event = NULL
};

G_MODULE_EXPORT gboolean remmina_plugin_entry(RemminaPluginService *service)
{
    remmina_plugin_service = service;
    gboolean ok = service->register_plugin((RemminaPlugin *)&remmina_plugin_ultraviewer);
    if (ok) {
        /* Run every 300ms to detect main window creation (at startup or reopen from tray) */
        g_timeout_add(300, inject_quick_connect_timer, NULL);
    }
    return ok;
}
