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

#include "uv_session.h"
#include "uv_wire.h"
#include "uv_crypto.h"
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>

typedef struct {
    bool format_flag;
    uint8_t *payload;
    size_t len;
} OutboundMsg;

static void free_outbound_msg(gpointer data)
{
    if (data) {
        OutboundMsg *m = (OutboundMsg *)data;
        g_free(m->payload);
        g_free(m);
    }
}

UvSession *uv_session_new(const char *partner_id,
                          const char *partner_password,
                          const char *display_name,
                          const char *dict_path,
                          int quality_preset,
                          bool insecure_tls,
                          gpointer user_data)
{
    UvSession *session = g_new0(UvSession, 1);
    g_strlcpy(session->partner_id, partner_id ? partner_id : "", sizeof(session->partner_id));
    g_strlcpy(session->partner_password, partner_password ? partner_password : "", sizeof(session->partner_password));

    char default_name[64];
    if (display_name && *display_name && strcmp(display_name, "Remmina") != 0) {
        g_strlcpy(default_name, display_name, sizeof(default_name));
    } else {
        const char *host = g_get_host_name();
        if (host && *host) {
            snprintf(default_name, sizeof(default_name), "Remmina - %s", host);
        } else {
            snprintf(default_name, sizeof(default_name), "Remmina");
        }
    }
    g_strlcpy(session->display_name, default_name, sizeof(session->display_name));
    g_strlcpy(session->custom_dict_path, dict_path ? dict_path : "", sizeof(session->custom_dict_path));
    session->quality_preset = quality_preset > 0 ? quality_preset : 45;
    session->insecure_tls = insecure_tls;
    session->callback_data = user_data;

    session->outbound_queue = g_async_queue_new_full(free_outbound_msg);
    session->cursor_mgr = uv_cursor_manager_new();

    if (pipe(session->wake_pipe) == 0) {
        int flags0 = fcntl(session->wake_pipe[0], F_GETFL, 0);
        if (flags0 >= 0) fcntl(session->wake_pipe[0], F_SETFL, flags0 | O_NONBLOCK);
        int flags1 = fcntl(session->wake_pipe[1], F_GETFL, 0);
        if (flags1 >= 0) fcntl(session->wake_pipe[1], F_SETFL, flags1 | O_NONBLOCK);
    } else {
        session->wake_pipe[0] = -1;
        session->wake_pipe[1] = -1;
    }

    return session;
}

void uv_session_free(UvSession *session)
{
    if (!session) return;

    uv_session_stop(session);

    if (session->renderer) {
        uv_screen_renderer_free(session->renderer);
        session->renderer = NULL;
    }
    if (session->cursor_mgr) {
        uv_cursor_manager_free(session->cursor_mgr);
        session->cursor_mgr = NULL;
    }
    if (session->outbound_queue) {
        g_async_queue_unref(session->outbound_queue);
        session->outbound_queue = NULL;
    }
    if (session->wake_pipe[0] >= 0) {
        close(session->wake_pipe[0]);
        close(session->wake_pipe[1]);
        session->wake_pipe[0] = -1;
        session->wake_pipe[1] = -1;
    }

    g_free(session);
}

void uv_session_queue_outbound(UvSession *session, bool format_flag, const uint8_t *payload, size_t len)
{
    if (!session || !session->running || !payload || len == 0) return;

    OutboundMsg *msg = g_new0(OutboundMsg, 1);
    msg->format_flag = format_flag;
    msg->payload = g_memdup2(payload, len);
    msg->len = len;

    g_async_queue_push(session->outbound_queue, msg);

    /* Wake worker poll */
    if (session->wake_pipe[1] >= 0) {
        uint8_t b = 1;
        if (write(session->wake_pipe[1], &b, 1) < 0) {
            /* ignore */
        }
    }
}

void uv_session_send_chat(UvSession *session, const char *text)
{
    if (!session || !text || !*text) return;

    char *msg_str = g_strdup_printf("12|||%s", text);
    uint8_t *utf16 = NULL;
    size_t utf16_len = 0;
    if (uv_utf8_to_utf16le(msg_str, &utf16, &utf16_len)) {
        uv_session_queue_outbound(session, true, utf16, utf16_len);
        g_free(utf16);
    }
    g_free(msg_str);
}

void uv_session_send_clipboard(UvSession *session, const char *text)
{
    if (!session || !text) return;

    uint8_t *payload = NULL;
    size_t len = 0;
    bool format_flag = false;
    if (uv_clipboard_encode_viewer_to_host(text, &payload, &len, &format_flag)) {
        uv_session_queue_outbound(session, format_flag, payload, len);
        g_free(payload);
    }
}

void uv_session_send_action(UvSession *session, const char *action_code)
{
    if (!session || !action_code) return;

    uint8_t *utf16 = NULL;
    size_t utf16_len = 0;
    if (uv_utf8_to_utf16le(action_code, &utf16, &utf16_len)) {
        uv_session_queue_outbound(session, true, utf16, utf16_len);
        g_free(utf16);
    }
}

static void drain_outbound_queue(UvSession *session)
{
    if (!session->relay_sock || !session->connected) return;

    OutboundMsg *msg = NULL;
    while ((msg = g_async_queue_try_pop(session->outbound_queue)) != NULL) {
        uint8_t *env = NULL;
        size_t env_len = 0;
        if (uv_forward_envelope_encode(session->partner_lookup.partner_ip,
                                       session->partner_lookup.target_port,
                                       msg->format_flag,
                                       msg->payload, msg->len,
                                       &env, &env_len)) {
            uv_tls_write_frame(session->relay_sock, env, env_len);
            g_free(env);
        }
        free_outbound_msg(msg);
    }
}

static gpointer uv_session_worker(gpointer user_data)
{
    UvSession *session = (UvSession *)user_data;
    char *err_msg = NULL;

    /* 1. Connect Command Channel */
    session->cmd_sock = uv_tls_connect(UV_DEFAULT_COMMAND_HOST, UV_DEFAULT_PORT,
                                       UV_DEFAULT_COMMAND_HOST, session->insecure_tls, &err_msg);
    if (!session->cmd_sock) {
        if (session->on_closed) session->on_closed(session, err_msg, session->callback_data);
        g_free(err_msg);
        return NULL;
    }

    /* 2. Client Registration */
    uv_identity_init(&session->client_ident);
    if (!uv_command_register(session->cmd_sock, &session->client_ident, &err_msg)) {
        if (session->on_closed) session->on_closed(session, err_msg, session->callback_data);
        g_free(err_msg);
        return NULL;
    }

    /* 3. Partner Lookup */
    if (!uv_command_lookup(session->cmd_sock, &session->client_ident,
                           session->partner_id, session->partner_password,
                           &session->partner_lookup, &err_msg)) {
        if (session->on_closed) session->on_closed(session, err_msg, session->callback_data);
        g_free(err_msg);
        return NULL;
    }

    /* 4. Connect Relay Channel */
    const char *relay_host = session->partner_lookup.relay_hostname[0] ?
                             session->partner_lookup.relay_hostname : UV_DEFAULT_COMMAND_HOST;

    session->relay_sock = uv_tls_connect(session->partner_lookup.relay_ip, UV_DEFAULT_PORT,
                                         relay_host,
                                         session->insecure_tls, &err_msg);
    if (!session->relay_sock) {
        if (session->on_closed) session->on_closed(session, err_msg, session->callback_data);
        g_free(err_msg);
        return NULL;
    }

    /* 5. Transmit Partner Login */
    uv_random_string(session->connection_hash, 16, "0123456789abcdefghijklmnopqrstuvwxyz");

    char *login_msg = uv_create_login_message(&session->client_ident,
                                              &session->partner_lookup,
                                              session->partner_password,
                                              session->display_name,
                                              0,
                                              session->quality_preset,
                                              session->connection_hash);

    uint8_t *login_env = NULL;
    size_t login_env_len = 0;
    uv_forward_envelope_encode(session->partner_lookup.partner_ip,
                               session->partner_lookup.target_port,
                               false,
                               (const uint8_t *)login_msg, strlen(login_msg),
                               &login_env, &login_env_len);
    g_free(login_msg);

    if (!uv_tls_write_frame(session->relay_sock, login_env, login_env_len)) {
        g_free(login_env);
        if (session->on_closed) session->on_closed(session, "Failed to send login message", session->callback_data);
        return NULL;
    }
    g_free(login_env);

    gint64 now = g_get_monotonic_time();
    session->last_relay_ping_time = now;
    session->last_peer_heartbeat_time = now;
    session->last_cmd_keepalive_time = now;

    /* 6. Event Loop */
    while (session->running) {
        struct pollfd pfds[3];
        pfds[0].fd = uv_tls_get_fd(session->relay_sock);
        pfds[0].events = POLLIN;
        pfds[1].fd = uv_tls_get_fd(session->cmd_sock);
        pfds[1].events = POLLIN;
        pfds[2].fd = session->wake_pipe[0];
        pfds[2].events = POLLIN;

        poll(pfds, 3, 200);
        if (!session->running) break;

        now = g_get_monotonic_time();

        /* Drain wake pipe if triggered */
        if (pfds[2].revents & POLLIN) {
            uint8_t dummy[64];
            while (read(session->wake_pipe[0], dummy, sizeof(dummy)) > 0) {}
        }

        /* Drain any queued outbound messages */
        drain_outbound_queue(session);

        /* Read relay socket */
        if ((pfds[0].revents & POLLIN) || (session->relay_sock && SSL_pending(session->relay_sock->ssl) > 0)) {
            uint8_t *payload = NULL;
            size_t payload_len = 0;
            if (uv_tls_read_frame(session->relay_sock, &payload, &payload_len, 5000, NULL)) {
                UvEnvelope env;
                if (uv_envelope_decode(payload, payload_len, &env)) {
                    if (env.type == UV_ENVELOPE_FORWARD && env.payload_len > 0) {
                        GArray *parts = NULL;
                        if (uv_multipart_decode(env.payload, env.payload_len, &parts) && parts->len > 0) {
                            const UvPart *p0 = &g_array_index(parts, UvPart, 0);
                            uint8_t cmd = p0->len > 0 ? p0->data[0] : 0xFF;

                            switch (cmd) {
                            case 0: { /* Screen Initialization */
                                UvScreenInit init;
                                if (uv_parse_screen_init(parts, &init, NULL)) {
                                    if (session->renderer) {
                                        uv_screen_renderer_free(session->renderer);
                                    }
                                    session->renderer = uv_screen_renderer_new(&init, session->custom_dict_path);
                                    session->connected = true;

                                    /* Ack initialization: send ASCII "17" */
                                    uv_session_queue_outbound(session, false, (const uint8_t *)"17", 2);

                                    /* Enable remote cursor: send "28|||1" */
                                    uv_session_queue_outbound(session, false, (const uint8_t *)"28|||1", 6);
                                    drain_outbound_queue(session);

                                    if (session->on_connected) {
                                        session->on_connected(session, init.width, init.height, session->callback_data);
                                    }
                                }
                                break;
                            }
                            case 1: { /* Screen Frame */
                                if (session->renderer) {
                                    if (uv_screen_apply_frame(session->renderer, parts, NULL)) {
                                        /* Ack frame: send ASCII "18" */
                                        uv_session_queue_outbound(session, false, (const uint8_t *)"18", 2);
                                        drain_outbound_queue(session);

                                        if (session->on_frame_ready) {
                                            session->on_frame_ready(session, session->callback_data);
                                        }
                                    } else {
                                        /* Decode error, still ack to maintain sequence flow */
                                        uv_session_queue_outbound(session, false, (const uint8_t *)"18", 2);
                                        drain_outbound_queue(session);
                                    }
                                }
                                break;
                            }
                            case 3: { /* Cursor selection */
                                GdkCursor *cur = NULL;
                                if (uv_cursor_handle_selection(session->cursor_mgr, parts, &cur) && cur) {
                                    if (session->on_cursor_change) {
                                        session->on_cursor_change(session, cur, session->callback_data);
                                    }
                                }
                                break;
                            }
                            case 4: { /* Cursor shape transfer */
                                GdkCursor *cur = NULL;
                                if (uv_cursor_handle_shape_transfer(session->cursor_mgr, parts, &cur) && cur) {
                                    if (session->on_cursor_change) {
                                        session->on_cursor_change(session, cur, session->callback_data);
                                    }
                                }
                                break;
                            }
                            case 12: { /* In-session Chat */
                                if (parts->len >= 2) {
                                    const UvPart *p1 = &g_array_index(parts, UvPart, 1);
                                    char *chat_text = NULL;
                                    if (uv_utf16le_to_utf8(p1->data, p1->len, &chat_text)) {
                                        if (session->on_chat_received) {
                                            session->on_chat_received(session, chat_text, session->callback_data);
                                        }
                                        g_free(chat_text);
                                    }
                                }
                                break;
                            }
                            case 28: { /* Remote cursor coordinates */
                                int16_t rx = 0, ry = 0;
                                uv_cursor_handle_position(session->cursor_mgr, parts, &rx, &ry);
                                break;
                            }
                            case 33: { /* Text clipboard */
                                char *clip_text = uv_clipboard_decode_host_to_viewer(parts);
                                if (clip_text) {
                                    if (session->on_clipboard_received) {
                                        session->on_clipboard_received(session, clip_text, session->callback_data);
                                    }
                                    g_free(clip_text);
                                }
                                break;
                            }
                            case 48: { /* Tile size update */
                                if (session->renderer) {
                                    uv_screen_set_tile_size(session->renderer, parts);
                                }
                                break;
                            }
                            case 27:
                            case 41:
                            case 54: { /* Remote disconnected */
                                session->running = false;
                                break;
                            }
                            default:
                                break;
                            }
                            g_array_free(parts, TRUE);
                        } else if (env.format_flag) {
                            /* UTF-16LE text messages (legacy clipboard or chat) */
                            char *clip_legacy = uv_clipboard_decode_legacy(env.payload, env.payload_len);
                            if (clip_legacy) {
                                if (session->on_clipboard_received) {
                                    session->on_clipboard_received(session, clip_legacy, session->callback_data);
                                }
                                g_free(clip_legacy);
                            }
                        }
                    }
                }
                g_free(payload);
            }
        }

        /* Read command socket (drain keepalives) */
        if (pfds[1].revents & POLLIN) {
            uint8_t *cmd_payload = NULL;
            size_t cmd_len = 0;
            if (uv_tls_read_frame(session->cmd_sock, &cmd_payload, &cmd_len, 0, NULL)) {
                g_free(cmd_payload);
            }
        }

        /* 10-second Relay Keepalive Ping */
        if (now - session->last_relay_ping_time >= 10 * G_USEC_PER_SEC) {
            uint8_t *ping_env = NULL;
            size_t ping_len = 0;
            uv_system_envelope_encode((const uint8_t *)"ping", 4, false, &ping_env, &ping_len);
            uv_tls_write_frame(session->relay_sock, ping_env, ping_len);
            g_free(ping_env);
            session->last_relay_ping_time = now;
        }

        /* 10-second Peer Heartbeat ("20") */
        if (session->connected && now - session->last_peer_heartbeat_time >= 10 * G_USEC_PER_SEC) {
            uv_session_queue_outbound(session, false, (const uint8_t *)"20", 2);
            session->last_peer_heartbeat_time = now;
        }

        /* 60-second Command Keepalive (3DES("2")) */
        if (now - session->last_cmd_keepalive_time >= 60 * G_USEC_PER_SEC) {
            uint8_t *ct = NULL;
            size_t ct_len = 0;
            if (uv_server_cipher_encrypt((const uint8_t *)"2", 1, &ct, &ct_len)) {
                uint8_t *cmd_env = NULL;
                size_t cmd_env_len = 0;
                uv_system_envelope_encode(ct, ct_len, true, &cmd_env, &cmd_env_len);
                g_free(ct);
                uv_tls_write_frame(session->cmd_sock, cmd_env, cmd_env_len);
                g_free(cmd_env);
            }
            session->last_cmd_keepalive_time = now;
        }
    }

    /* Send disconnect notice Command 19 before closing */
    if (session->relay_sock && session->connected) {
        uint8_t *env = NULL;
        size_t env_len = 0;
        uv_forward_envelope_encode(session->partner_lookup.partner_ip,
                                   session->partner_lookup.target_port,
                                   false,
                                   (const uint8_t *)"19", 2,
                                   &env, &env_len);
        uv_tls_write_frame(session->relay_sock, env, env_len);
        g_free(env);
    }

    if (session->relay_sock) {
        uv_tls_close(session->relay_sock);
        session->relay_sock = NULL;
    }
    if (session->cmd_sock) {
        uv_tls_close(session->cmd_sock);
        session->cmd_sock = NULL;
    }

    if (session->on_closed) {
        session->on_closed(session, "Session ended", session->callback_data);
    }

    return NULL;
}

bool uv_session_start(UvSession *session)
{
    if (!session || session->running) return false;
    session->running = true;
    session->connected = false;
    session->worker_thread = g_thread_new("uv-session", uv_session_worker, session);
    return (session->worker_thread != NULL);
}

void uv_session_stop(UvSession *session)
{
    if (!session || !session->running) return;
    session->running = false;

    if (session->wake_pipe[1] >= 0) {
        uint8_t b = 1;
        if (write(session->wake_pipe[1], &b, 1) < 0) {}
    }

    if (session->relay_sock) {
        int fd = uv_tls_get_fd(session->relay_sock);
        if (fd >= 0) shutdown(fd, SHUT_RDWR);
    }
    if (session->cmd_sock) {
        int fd = uv_tls_get_fd(session->cmd_sock);
        if (fd >= 0) shutdown(fd, SHUT_RDWR);
    }

    if (session->worker_thread) {
        g_thread_join(session->worker_thread);
        session->worker_thread = NULL;
    }
}
