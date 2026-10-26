/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Session State Machine & Worker
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UV_SESSION_H
#define UV_SESSION_H

#include "uv_types.h"
#include "uv_tls.h"
#include "uv_command.h"
#include "uv_screen.h"
#include "uv_cursor.h"
#include "uv_clipboard.h"
#include "uv_input.h"
#include "uv_audio.h"
#include "uv_file_transfer.h"

typedef struct _UvSession UvSession;

typedef void (*UvSessionConnectedCb)(UvSession *session, int width, int height, gpointer user_data);
typedef void (*UvSessionFrameReadyCb)(UvSession *session, gpointer user_data);
typedef void (*UvSessionCursorChangeCb)(UvSession *session, GdkCursor *cursor, gpointer user_data);
typedef void (*UvSessionCursorPositionCb)(UvSession *session, int16_t x, int16_t y, gpointer user_data);
typedef void (*UvSessionClipboardCb)(UvSession *session, const char *text, gpointer user_data);
typedef void (*UvSessionChatCb)(UvSession *session, const char *text, gpointer user_data);
typedef void (*UvSessionClosedCb)(UvSession *session, const char *error_msg, gpointer user_data);

struct _UvSession {
    /* Credentials & Connection Params */
    char partner_id[32];
    char partner_password[64];
    char display_name[32];
    char custom_dict_path[256];
    int quality_preset;
    bool insecure_tls;

    /* State */
    volatile bool running;
    volatile bool connected;
    int wake_pipe[2];
    GThread *worker_thread;

    /* Sockets */
    UvTlsSocket *cmd_sock;
    UvTlsSocket *relay_sock;

    /* Identities */
    UvClientIdentity client_ident;
    UvPartnerLookup partner_lookup;
    char connection_hash[17];

    /* Subsystems */
    UvScreenRenderer *renderer;
    UvCursorManager *cursor_mgr;
    bool show_cursor;
    UvAudio *audio;
    bool sound_enabled;
    UvFileTransferManager *ft_mgr;

    /* Outgoing Queue */
    GAsyncQueue *outbound_queue;

    /* Timers */
    gint64 last_relay_ping_time;
    gint64 last_peer_heartbeat_time;
    gint64 last_cmd_keepalive_time;

    /* Callbacks */
    UvSessionConnectedCb on_connected;
    UvSessionFrameReadyCb on_frame_ready;
    UvSessionCursorChangeCb on_cursor_change;
    UvSessionCursorPositionCb on_cursor_position;
    UvSessionClipboardCb on_clipboard_received;
    UvSessionChatCb on_chat_received;
    UvSessionClosedCb on_closed;
    gpointer callback_data;
};

UvSession *uv_session_new(const char *partner_id,
                          const char *partner_password,
                          const char *display_name,
                          const char *dict_path,
                          int quality_preset,
                          bool insecure_tls,
                          gpointer user_data);

void uv_session_free(UvSession *session);

bool uv_session_start(UvSession *session);

void uv_session_stop(UvSession *session);

void uv_session_queue_outbound(UvSession *session, bool format_flag, const uint8_t *payload, size_t len);

void uv_session_send_chat(UvSession *session, const char *text);

void uv_session_send_clipboard(UvSession *session, const char *text);

void uv_session_send_action(UvSession *session, const char *action_code);

void uv_session_toggle_sound(UvSession *session, bool enabled);

bool uv_session_send_file(UvSession *session, const char *local_path);

#endif /* UV_SESSION_H */
