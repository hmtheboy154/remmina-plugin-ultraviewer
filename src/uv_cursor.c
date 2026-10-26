/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Remote Hardware Cursor
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "uv_cursor.h"
#include "uv_wire.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <zstd.h>

static void free_gdk_cursor(gpointer data)
{
    if (data) {
        g_object_unref(G_OBJECT(data));
    }
}

UvCursorManager *uv_cursor_manager_new(void)
{
    UvCursorManager *mgr = g_new0(UvCursorManager, 1);
    mgr->cache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free_gdk_cursor);
    g_mutex_init(&mgr->mutex);
    return mgr;
}

void uv_cursor_manager_free(UvCursorManager *mgr)
{
    if (!mgr) return;
    if (mgr->current_cursor) {
        g_object_unref(mgr->current_cursor);
    }
    g_hash_table_destroy(mgr->cache);
    g_mutex_clear(&mgr->mutex);
    g_free(mgr);
}

static GdkCursor *decode_cur_file(const uint8_t *data, size_t len)
{
    if (!data || len < 22) return NULL;

    /* ICONDIR header: reserved=0, type=2 (CUR), count >= 1 */
    if (uv_read_u16_le(data) != 0 || uv_read_u16_le(data + 2) != 2) return NULL;
    uint16_t count = uv_read_u16_le(data + 4);
    if (count == 0 || 6 + (size_t)count * 16 > len) return NULL;

    (void)data[6]; /* width */
    (void)data[7]; /* height */
    uint16_t hot_x = uv_read_u16_le(data + 10);
    uint16_t hot_y = uv_read_u16_le(data + 12);
    uint32_t res_size = uv_read_u32_le(data + 14);
    uint32_t res_offset = uv_read_u32_le(data + 18);

    if (res_offset + res_size > len || res_size == 0) return NULL;
    const uint8_t *res = data + res_offset;

    cairo_surface_t *surface = NULL;

    /* Check for embedded PNG */
    static const uint8_t PNG_MAGIC[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    if (res_size >= 8 && memcmp(res, PNG_MAGIC, 8) == 0) {
        GdkPixbufLoader *loader = gdk_pixbuf_loader_new_with_type("png", NULL);
        if (loader) {
            if (gdk_pixbuf_loader_write(loader, res, res_size, NULL) &&
                gdk_pixbuf_loader_close(loader, NULL)) {
                GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(loader);
                if (pb) {
                    surface = gdk_cairo_surface_create_from_pixbuf(pb, 1, NULL);
                }
            }
            g_object_unref(loader);
        }
    } else if (res_size >= 40) {
        /* Standard Windows DIB Header: BITMAPINFOHEADER */
        uint32_t bi_size = uv_read_u32_le(res);
        if (bi_size == 40) {
            int32_t bi_width = uv_read_i32_le(res + 4);
            int32_t bi_height = uv_read_i32_le(res + 8) / 2; /* In CUR, biHeight is 2x icon height */
            uint16_t bi_bitcount = uv_read_u16_le(res + 14);

            if (bi_width > 0 && bi_height > 0 && bi_bitcount == 32) {
                surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, bi_width, bi_height);
                uint32_t *surf_data = (uint32_t *)cairo_image_surface_get_data(surface);
                int stride_dwords = cairo_image_surface_get_stride(surface) / 4;

                const uint8_t *src_pixels = res + 40;
                /* DIB rows are bottom-up, 32bpp BGRA */
                for (int y = 0; y < bi_height; y++) {
                    const uint8_t *src_row = src_pixels + (size_t)(bi_height - 1 - y) * (size_t)bi_width * 4;
                    uint32_t *dst_row = surf_data + (size_t)y * (size_t)stride_dwords;
                    for (int x = 0; x < bi_width; x++) {
                        uint8_t b = src_row[x * 4];
                        uint8_t g = src_row[x * 4 + 1];
                        uint8_t r = src_row[x * 4 + 2];
                        uint8_t a = src_row[x * 4 + 3];
                        dst_row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                    }
                }
                cairo_surface_mark_dirty(surface);
            }
        }
    }

    if (!surface) return NULL;

    GdkDisplay *display = gdk_display_get_default();
    if (!display) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    GdkCursor *cursor = gdk_cursor_new_from_surface(display, surface, (double)hot_x, (double)hot_y);
    cairo_surface_destroy(surface);
    return cursor;
}

bool uv_cursor_handle_selection(UvCursorManager *mgr, const GArray *parts, GdkCursor **out_cursor)
{
    if (!mgr || !parts || parts->len < 2) return false;

    const UvPart *p1 = &g_array_index(parts, UvPart, 1);
    if (p1->len < 4) return false;

    int32_t hash = uv_read_i32_le(p1->data);
    if (hash == UV_IGNORED_SELECTION_HASH) {
        return true;
    }

    g_mutex_lock(&mgr->mutex);
    GdkCursor *cur = g_hash_table_lookup(mgr->cache, GINT_TO_POINTER(hash));
    if (cur) {
        mgr->current_hash = hash;
        if (mgr->current_cursor) g_object_unref(mgr->current_cursor);
        mgr->current_cursor = g_object_ref(cur);
        if (out_cursor) *out_cursor = g_object_ref(cur);
    }
    g_mutex_unlock(&mgr->mutex);

    return (cur != NULL);
}

bool uv_cursor_handle_shape_transfer(UvCursorManager *mgr, const GArray *parts, GdkCursor **out_cursor)
{
    if (!mgr || !parts || parts->len < 4) return false;

    const UvPart *p1 = &g_array_index(parts, UvPart, 1);
    const UvPart *p2 = &g_array_index(parts, UvPart, 2);
    const UvPart *p3 = &g_array_index(parts, UvPart, 3);

    if (p1->len < 4 || p2->len < 4 || p3->len == 0) return false;

    int32_t hash = uv_read_i32_le(p1->data);
    int32_t expected_size = uv_read_i32_le(p2->data);
    if (expected_size <= 0 || expected_size > (2 * 1024 * 1024)) return false;

    /* Decompress with Zstd without dictionary */
    uint8_t *cur_buf = g_malloc(expected_size);
    size_t dec_ret = ZSTD_decompress(cur_buf, (size_t)expected_size, p3->data, p3->len);
    if (ZSTD_isError(dec_ret) || dec_ret != (size_t)expected_size) {
        g_free(cur_buf);
        return false;
    }

    GdkCursor *cursor = decode_cur_file(cur_buf, dec_ret);
    g_free(cur_buf);

    if (!cursor) return false;

    g_mutex_lock(&mgr->mutex);
    g_hash_table_insert(mgr->cache, GINT_TO_POINTER(hash), g_object_ref(cursor));
    mgr->current_hash = hash;
    if (mgr->current_cursor) g_object_unref(mgr->current_cursor);
    mgr->current_cursor = g_object_ref(cursor);

    if (out_cursor) *out_cursor = g_object_ref(cursor);
    g_mutex_unlock(&mgr->mutex);

    g_object_unref(cursor);
    return true;
}

bool uv_cursor_handle_position(UvCursorManager *mgr, const GArray *parts, int16_t *out_x, int16_t *out_y)
{
    if (!mgr || !parts || parts->len < 3) return false;

    const UvPart *p1 = &g_array_index(parts, UvPart, 1);
    const UvPart *p2 = &g_array_index(parts, UvPart, 2);
    if (p1->len < 2 || p2->len < 2) return false;

    int16_t x = uv_read_i16_le(p1->data);
    int16_t y = uv_read_i16_le(p2->data);

    g_mutex_lock(&mgr->mutex);
    mgr->remote_x = x;
    mgr->remote_y = y;
    g_mutex_unlock(&mgr->mutex);

    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    return true;
}
