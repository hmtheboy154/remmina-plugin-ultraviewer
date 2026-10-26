/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Text Clipboard Synchronization
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "uv_clipboard.h"
#include "uv_crypto.h"
#include "uv_wire.h"
#include <zstd.h>

bool uv_clipboard_encode_viewer_to_host(const char *text,
                                        uint8_t **out_payload,
                                        size_t *out_len,
                                        bool *out_format_flag)
{
    if (!text || *text == '\0') {
        /* Empty text uses legacy Command 5 with format_flag = true */
        uint8_t *utf16 = NULL;
        size_t utf16_len = 0;
        if (!uv_utf8_to_utf16le("5|||", &utf16, &utf16_len)) return false;
        *out_payload = utf16;
        *out_len = utf16_len;
        *out_format_flag = true;
        return true;
    }

    uint8_t *utf16 = NULL;
    size_t utf16_len = 0;
    if (!uv_utf8_to_utf16le(text, &utf16, &utf16_len)) return false;

    if (utf16_len > 1024 * 1024) {
        g_free(utf16);
        return false;
    }

    size_t bound = ZSTD_compressBound(utf16_len);
    uint8_t *compressed = g_malloc(bound);
    size_t comp_size = ZSTD_compress(compressed, bound, utf16, utf16_len, 3);
    g_free(utf16);

    if (ZSTD_isError(comp_size)) {
        g_free(compressed);
        return false;
    }

    char header[64];
    snprintf(header, sizeof(header), "33|||%zu|||", utf16_len);
    size_t hdr_len = strlen(header);

    uint8_t *payload = g_malloc(hdr_len + comp_size);
    memcpy(payload, header, hdr_len);
    memcpy(payload + hdr_len, compressed, comp_size);
    g_free(compressed);

    *out_payload = payload;
    *out_len = hdr_len + comp_size;
    *out_format_flag = false;
    return true;
}

char *uv_clipboard_decode_host_to_viewer(const GArray *parts)
{
    if (!parts || parts->len < 2) return NULL;

    const UvPart *p0 = &g_array_index(parts, UvPart, 0);
    if (p0->len < 1) return NULL;

    if (p0->data[0] == 33) {
        if (parts->len < 3) return NULL;
        const UvPart *p1 = &g_array_index(parts, UvPart, 1);
        const UvPart *p2 = &g_array_index(parts, UvPart, 2);
        if (p1->len < 4) return NULL;

        uint32_t expected_size = uv_read_u32_le(p1->data);
        if (expected_size == 0 || expected_size > 1024 * 1024) return NULL;

        uint8_t *utf16 = g_malloc(expected_size);
        size_t dec_ret = ZSTD_decompress(utf16, expected_size, p2->data, p2->len);
        if (ZSTD_isError(dec_ret) || dec_ret != expected_size) {
            g_free(utf16);
            return NULL;
        }

        char *utf8 = NULL;
        uv_utf16le_to_utf8(utf16, expected_size, &utf8);
        g_free(utf16);
        return utf8;
    } else if (p0->data[0] == 5) {
        const UvPart *p1 = &g_array_index(parts, UvPart, 1);
        if (p1->len == 0) return g_strdup("");
        char *utf8 = NULL;
        if (uv_utf16le_to_utf8(p1->data, p1->len, &utf8)) {
            return utf8;
        }
    }

    return NULL;
}

char *uv_clipboard_decode_legacy(const uint8_t *payload, size_t len)
{
    if (!payload || len < 6) return NULL;

    char *utf8 = NULL;
    if (!uv_utf16le_to_utf8(payload, len, &utf8)) return NULL;

    if (g_str_has_prefix(utf8, "5|||")) {
        char *result = g_strdup(utf8 + 4);
        g_free(utf8);
        return result;
    }

    g_free(utf8);
    return NULL;
}
