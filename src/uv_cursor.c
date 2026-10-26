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
#include <string.h>

static void free_cursor_item(gpointer data)
{
    if (data) {
        UvCursorItem *item = (UvCursorItem *)data;
        if (item->cursor) {
            g_object_unref(item->cursor);
        }
        if (item->surface) {
            cairo_surface_destroy(item->surface);
        }
        g_free(item);
    }
}

UvCursorManager *uv_cursor_manager_new(void)
{
    UvCursorManager *mgr = g_new0(UvCursorManager, 1);
    mgr->cache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free_cursor_item);
    g_mutex_init(&mgr->mutex);
    return mgr;
}

void uv_cursor_manager_free(UvCursorManager *mgr)
{
    if (!mgr) return;
    g_hash_table_destroy(mgr->cache);
    g_mutex_clear(&mgr->mutex);
    g_free(mgr);
}

static UvCursorItem *create_default_arrow_item(void)
{
    static const char *rows[16] = {
        "X..........", "XX.........", "XOX........", "XOOX.......", "XOOOX......", "XOOOOX.....",
        "XOOOOOX....", "XOOOOOOX...", "XOOOOOOOX..", "XOOOOOOOOX.", "XOOOOOXXXXX", "XOOXOOX....",
        "XOX.XOOX...", "XX..XOOX...", "X....XOOX..", ".....XXX...",
    };
    int w = 11, h = 16;
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    uint32_t *dst = (uint32_t *)cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf) / 4;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            char c = rows[y][x];
            uint32_t px = 0;
            if (c == 'X') px = 0xFF000000;
            else if (c == 'O') px = 0xFFFFFFFF;
            dst[y * stride + x] = px;
        }
    }
    cairo_surface_mark_dirty(surf);

    GdkDisplay *display = gdk_display_get_default();
    if (!display) {
        cairo_surface_destroy(surf);
        return NULL;
    }

    GdkCursor *cur = gdk_cursor_new_from_surface(display, surf, 0.0, 0.0);
    if (!cur) {
        cairo_surface_destroy(surf);
        return NULL;
    }

    UvCursorItem *item = g_new0(UvCursorItem, 1);
    item->cursor = cur;
    item->surface = surf;
    item->hot_x = 0;
    item->hot_y = 0;
    return item;
}

static cairo_surface_t *decode_dib_to_surface(const uint8_t *b, size_t len,
                                              int width, int height)
{
    if (len < 16) return NULL;
    uint32_t header = uv_read_u32_le(b);
    if (header != 12 && header != 40 && header != 52 && header != 56 && header != 108 && header != 124) {
        return NULL;
    }
    if (header > len) return NULL;

    bool core = (header == 12);
    int bits = core ? uv_read_u16_le(b + 10) : uv_read_u16_le(b + 14);
    if (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32) {
        return NULL;
    }
    if (!core && len < 40) return NULL;

    bool top_down = !core && (uv_read_i32_le(b + 8) < 0);
    size_t declared = core ? 0 : uv_read_u32_le(b + 32);
    size_t colors = 0;
    if (bits <= 8) {
        colors = (declared == 0) ? (1U << bits) : declared;
        if (colors > (1U << bits)) return NULL;
    } else {
        if (declared > 256) return NULL;
        colors = declared;
    }

    size_t pal_stride = core ? 3 : 4;
    size_t px_off = header + colors * pal_stride;
    size_t stride = ((width * bits + 31) / 32) * 4;
    size_t mask_stride = ((width + 31) / 32) * 4;
    size_t mask_off = px_off + stride * height;

    if (px_off > len || mask_off > len || mask_stride * height > len - mask_off) {
        return NULL;
    }

    bool uses_alpha = false;
    if (bits == 32) {
        for (int r = 0; r < height; r++) {
            for (int x = 0; x < width; x++) {
                if (b[px_off + r * stride + x * 4 + 3] != 0) {
                    uses_alpha = true;
                    break;
                }
            }
            if (uses_alpha) break;
        }
    }

    uint32_t *pixels = g_malloc0(sizeof(uint32_t) * width * height);
    bool *inverted = g_malloc0(sizeof(bool) * width * height);

    for (int y = 0; y < height; y++) {
        int sy = top_down ? y : (height - 1 - y);
        size_t row = px_off + sy * stride;
        size_t mrow = mask_off + sy * mask_stride;

        for (int x = 0; x < width; x++) {
            uint8_t bl = 0, g = 0, r = 0, a = 255;

            switch (bits) {
            case 1:
            case 4:
            case 8: {
                size_t i = 0;
                if (bits == 1) {
                    i = (b[row + x / 8] >> (7 - (x % 8))) & 1;
                } else if (bits == 4) {
                    i = (b[row + x / 2] >> ((x % 2 == 0) ? 4 : 0)) & 0x0F;
                } else {
                    i = b[row + x];
                }
                if (i >= colors) {
                    g_free(pixels);
                    g_free(inverted);
                    return NULL;
                }
                size_t e = header + i * pal_stride;
                bl = b[e];
                g = b[e + 1];
                r = b[e + 2];
                break;
            }
            case 16: {
                uint16_t v = uv_read_u16_le(b + row + x * 2);
                bl = ((v & 0x1F) << 3) | ((v & 0x1F) >> 2);
                g = (((v >> 5) & 0x1F) << 3) | (((v >> 5) & 0x1F) >> 2);
                r = (((v >> 10) & 0x1F) << 3) | (((v >> 10) & 0x1F) >> 2);
                break;
            }
            case 24: {
                size_t o = row + x * 3;
                bl = b[o];
                g = b[o + 1];
                r = b[o + 2];
                break;
            }
            case 32: {
                size_t o = row + x * 4;
                bl = b[o];
                g = b[o + 1];
                r = b[o + 2];
                if (uses_alpha) a = b[o + 3];
                break;
            }
            }

            if (!uses_alpha && (b[mrow + x / 8] & (0x80 >> (x % 8))) != 0) {
                if (bl != 0 || g != 0 || r != 0) {
                    inverted[y * width + x] = true;
                    continue;
                }
                a = 0;
            }

            pixels[y * width + x] = (a == 0) ? 0 :
                (((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | bl);
        }
    }

    /* Process inverted pixels (composite with white & black outline) */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (!inverted[y * width + x]) continue;
            pixels[y * width + x] = 0xFFFFFFFF;
            static const int dxs[4] = { -1, 1, 0, 0 };
            static const int dys[4] = { 0, 0, -1, 1 };
            for (int d = 0; d < 4; d++) {
                int nx = x + dxs[d];
                int ny = y + dys[d];
                if (nx >= 0 && ny >= 0 && nx < width && ny < height) {
                    int n = ny * width + nx;
                    if (!inverted[n] && pixels[n] == 0) {
                        pixels[n] = 0xFF000000;
                    }
                }
            }
        }
    }
    g_free(inverted);

    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    uint32_t *dst = (uint32_t *)cairo_image_surface_get_data(surf);
    int surf_stride = cairo_image_surface_get_stride(surf) / 4;

    for (int y = 0; y < height; y++) {
        memcpy(dst + y * surf_stride, pixels + y * width, width * sizeof(uint32_t));
    }
    cairo_surface_mark_dirty(surf);
    g_free(pixels);

    return surf;
}

static UvCursorItem *decode_cur_file(const uint8_t *data, size_t len)
{
    if (!data || len < 22) return NULL;

    /* ICONDIR header: reserved=0, type=2 (CUR), count >= 1 */
    if (uv_read_u16_le(data) != 0 || uv_read_u16_le(data + 2) != 2) return NULL;
    uint16_t count = uv_read_u16_le(data + 4);
    if (count == 0 || 6 + (size_t)count * 16 > len) return NULL;

    int width = (data[6] == 0) ? 256 : data[6];
    int height = (data[7] == 0) ? 256 : data[7];
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
    } else {
        surface = decode_dib_to_surface(res, res_size, width, height);
    }

    if (!surface) {
        return create_default_arrow_item();
    }

    GdkDisplay *display = gdk_display_get_default();
    if (!display) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    GdkCursor *cursor = gdk_cursor_new_from_surface(display, surface, (double)hot_x, (double)hot_y);
    if (!cursor) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    UvCursorItem *item = g_new0(UvCursorItem, 1);
    item->cursor = cursor;
    item->surface = surface;
    item->hot_x = (int)hot_x;
    item->hot_y = (int)hot_y;
    return item;
}

bool uv_cursor_handle_selection(UvCursorManager *mgr, const GArray *parts,
                                GdkCursor **out_cursor, int32_t *out_missing_hash)
{
    if (out_cursor) *out_cursor = NULL;
    if (out_missing_hash) *out_missing_hash = 0;
    if (!mgr || !parts || parts->len < 2) return false;

    const UvPart *p1 = &g_array_index(parts, UvPart, 1);
    if (p1->len < 4) return false;

    int32_t hash = uv_read_i32_le(p1->data);
    if (hash == UV_IGNORED_SELECTION_HASH) {
        return true;
    }

    g_mutex_lock(&mgr->mutex);
    UvCursorItem *item = g_hash_table_lookup(mgr->cache, GINT_TO_POINTER(hash));
    if (item) {
        mgr->current_hash = hash;
        mgr->current_item = item;
        if (out_cursor && item->cursor) {
            *out_cursor = g_object_ref(item->cursor);
        }
        g_mutex_unlock(&mgr->mutex);
        return true;
    }

    if (out_missing_hash) {
        *out_missing_hash = hash;
    }
    g_mutex_unlock(&mgr->mutex);
    return false;
}

bool uv_cursor_handle_shape_transfer(UvCursorManager *mgr, const GArray *parts,
                                     GdkCursor **out_cursor)
{
    if (out_cursor) *out_cursor = NULL;
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

    UvCursorItem *item = decode_cur_file(cur_buf, dec_ret);
    g_free(cur_buf);

    if (!item) {
        item = create_default_arrow_item();
    }
    if (!item) return false;

    g_mutex_lock(&mgr->mutex);
    g_hash_table_insert(mgr->cache, GINT_TO_POINTER(hash), item);
    mgr->current_hash = hash;
    mgr->current_item = item;

    if (out_cursor && item->cursor) {
        *out_cursor = g_object_ref(item->cursor);
    }
    g_mutex_unlock(&mgr->mutex);
    return true;
}

bool uv_cursor_handle_position(UvCursorManager *mgr, const GArray *parts,
                               int16_t *out_x, int16_t *out_y)
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

void uv_cursor_draw_remote(UvCursorManager *mgr, cairo_t *cr, int16_t x, int16_t y)
{
    if (!mgr || !cr) return;

    g_mutex_lock(&mgr->mutex);
    if (mgr->current_item && mgr->current_item->surface) {
        cairo_set_source_surface(cr, mgr->current_item->surface,
                                 (double)(x - mgr->current_item->hot_x),
                                 (double)(y - mgr->current_item->hot_y));
        cairo_paint(cr);
    } else {
        /* Fallback: draw a high-visibility pointer dot with white border */
        cairo_save(cr);
        cairo_set_line_width(cr, 1.5);
        cairo_arc(cr, (double)x, (double)y, 5.0, 0, 2 * G_PI);
        cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, 0.85);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_stroke(cr);
        cairo_restore(cr);
    }
    g_mutex_unlock(&mgr->mutex);
}
