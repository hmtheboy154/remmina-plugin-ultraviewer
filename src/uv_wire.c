/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Wire Framing & Envelopes
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "uv_wire.h"
#include <arpa/inet.h>

bool uv_frame_encode(const uint8_t *payload, size_t len,
                     uint8_t **out_frame, size_t *out_len)
{
    if (len > UV_MAX_FRAME_SIZE) return false;

    uint8_t *buf = g_malloc(4 + len);
    uv_write_u32_le(buf, (uint32_t)len);
    if (len > 0 && payload) {
        memcpy(buf + 4, payload, len);
    }
    *out_frame = buf;
    *out_len = 4 + len;
    return true;
}

bool uv_system_envelope_encode(const uint8_t *payload, size_t len, bool with_flag,
                               uint8_t **out_env, size_t *out_len)
{
    size_t header_len = with_flag ? 2 : 1;
    uint8_t *buf = g_malloc(header_len + len);
    buf[0] = 0x01;
    if (with_flag) {
        buf[1] = 0x00;
    }
    if (len > 0 && payload) {
        memcpy(buf + header_len, payload, len);
    }
    *out_env = buf;
    *out_len = header_len + len;
    return true;
}

static bool parse_ipv4(const char *s, uint8_t ip[4])
{
    struct in_addr addr;
    if (inet_pton(AF_INET, s, &addr) != 1) {
        return false;
    }
    uint32_t net = (uint32_t)addr.s_addr;
    ip[0] = (uint8_t)(net & 0xFF);
    ip[1] = (uint8_t)((net >> 8) & 0xFF);
    ip[2] = (uint8_t)((net >> 16) & 0xFF);
    ip[3] = (uint8_t)((net >> 24) & 0xFF);
    return true;
}

bool uv_forward_envelope_encode(const char *ip_str, uint16_t port, bool format_flag,
                                const uint8_t *payload, size_t len,
                                uint8_t **out_env, size_t *out_len)
{
    uint8_t ip[4];
    if (!parse_ipv4(ip_str, ip)) {
        return false;
    }

    uint8_t *buf = g_malloc(8 + len);
    buf[0] = 0x00;
    buf[1] = format_flag ? 0x01 : 0x00;
    /* Reversed IPv4: D, C, B, A */
    buf[2] = ip[3];
    buf[3] = ip[2];
    buf[4] = ip[1];
    buf[5] = ip[0];
    /* Port LE */
    uv_write_u16_le(buf + 6, port);

    if (len > 0 && payload) {
        memcpy(buf + 8, payload, len);
    }
    *out_env = buf;
    *out_len = 8 + len;
    return true;
}

bool uv_envelope_decode(const uint8_t *buf, size_t len, UvEnvelope *out_env)
{
    if (!buf || len == 0 || !out_env) return false;
    memset(out_env, 0, sizeof(*out_env));

    if (buf[0] == 0x00) {
        if (len < 8) return false;
        out_env->type = UV_ENVELOPE_FORWARD;
        out_env->format_flag = (buf[1] != 0);
        /* Reversed back: bytes [5], [4], [3], [2] */
        snprintf(out_env->ip, sizeof(out_env->ip), "%u.%u.%u.%u",
                 (unsigned int)buf[5], (unsigned int)buf[4],
                 (unsigned int)buf[3], (unsigned int)buf[2]);
        out_env->port = uv_read_u16_le(buf + 6);
        out_env->payload = buf + 8;
        out_env->payload_len = len - 8;
        return true;
    } else if (buf[0] == 0x01) {
        out_env->type = UV_ENVELOPE_SYSTEM;
        out_env->payload = buf + 1;
        out_env->payload_len = len - 1;
        return true;
    }

    return false;
}

bool uv_multipart_encode(const UvPart *parts, size_t n_parts,
                         uint8_t **out_buf, size_t *out_len)
{
    if (!parts || n_parts == 0) return false;

    size_t total = 0;
    for (size_t i = 0; i < n_parts; i++) {
        total += 4 + parts[i].len;
    }

    if (total > UV_MAX_MULTIPART_BYTES) return false;

    uint8_t *buf = g_malloc(total);
    size_t offset = 0;
    for (size_t i = 0; i < n_parts; i++) {
        uv_write_u32_le(buf + offset, (uint32_t)parts[i].len);
        offset += 4;
        if (parts[i].len > 0 && parts[i].data) {
            memcpy(buf + offset, parts[i].data, parts[i].len);
            offset += parts[i].len;
        }
    }

    *out_buf = buf;
    *out_len = total;
    return true;
}

bool uv_multipart_decode(const uint8_t *buf, size_t len,
                         GArray **out_parts)
{
    if (!buf || len == 0 || !out_parts) return false;

    GArray *parts = g_array_new(FALSE, FALSE, sizeof(UvPart));
    size_t offset = 0;

    while (offset < len) {
        if (parts->len >= UV_MAX_MULTIPART_PARTS) {
            g_array_free(parts, TRUE);
            return false;
        }
        if (len - offset < 4) {
            g_array_free(parts, TRUE);
            return false;
        }

        uint32_t part_len = uv_read_u32_le(buf + offset);
        offset += 4;

        if (len - offset < part_len) {
            g_array_free(parts, TRUE);
            return false;
        }

        UvPart p;
        p.data = buf + offset;
        p.len = part_len;
        g_array_append_val(parts, p);

        offset += part_len;
    }

    *out_parts = parts;
    return true;
}

char *uv_http_preamble_create(const char *host)
{
    return g_strdup_printf(
        "POST /UVService/ HTTP/1.1\r\n"
        "User-Agent: Mozilla/4.0 (compatible; MSIE5.01; Windows NT)\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Connection: Keep-Alive\r\n"
        "Content-length: 8589934590\r\n"
        "Host: %s\r\n"
        "\r\n",
        host
    );
}

char *uv_join_fields(const char *const *fields, size_t n_fields)
{
    GString *s = g_string_new("");
    for (size_t i = 0; i < n_fields; i++) {
        if (i > 0) g_string_append_c(s, ':');
        if (fields[i]) g_string_append(s, fields[i]);
    }
    return g_string_free(s, FALSE);
}

char **uv_split_fields(const char *str, char delimiter, int max_tokens)
{
    char delim_str[2] = { delimiter, '\0' };
    return g_strsplit(str, delim_str, max_tokens);
}
