/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Screen Frame Decoder & Framebuffer
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "uv_screen.h"
#include "uv_wire.h"

static inline uint32_t rgb555_to_xrgb8888(uint16_t v)
{
    /* RGB555: bit 0..4 = Blue, bit 5..9 = Green, bit 10..14 = Red (Windows GDI / UltraViewer wire) */
    uint32_t b = (v & 31);
    uint32_t g = (v >> 5) & 31;
    uint32_t r = (v >> 10) & 31;
    uint32_t r8 = (r << 3) | (r >> 2);
    uint32_t g8 = (g << 3) | (g >> 2);
    uint32_t b8 = (b << 3) | (b >> 2);
    return (r8 << 16) | (g8 << 8) | b8;
}

static inline uint16_t xrgb8888_to_rgb555(uint32_t px)
{
    uint16_t r5 = (uint16_t)((px >> 16) & 0xFF) >> 3;
    uint16_t g5 = (uint16_t)((px >> 8) & 0xFF) >> 3;
    uint16_t b5 = (uint16_t)(px & 0xFF) >> 3;
    return (r5 << 10) | (g5 << 5) | b5;
}

static uint8_t *load_screen_dictionary(const char *custom_path, size_t *out_size)
{
    const char *candidates[8];
    int n_cand = 0;

    if (custom_path && *custom_path) {
        candidates[n_cand++] = custom_path;
    }

    char *home_path = g_build_filename(g_get_user_config_dir(), "remmina", "ultraviewer", "8bit.dat", NULL);
    candidates[n_cand++] = "/usr/share/remmina/ultraviewer/8bit.dat";
    candidates[n_cand++] = "/usr/lib/remmina/plugins/8bit.dat";
    candidates[n_cand++] = home_path;
    candidates[n_cand++] = "assets/8bit.dat";
    candidates[n_cand++] = "../assets/8bit.dat";
    candidates[n_cand++] = "../MacViewer/LinuxViewer/assets/8bit.dat";
    candidates[n_cand++] = "../../MacViewer/LinuxViewer/assets/8bit.dat";

    uint8_t *data = NULL;
    gsize length = 0;

    for (int i = 0; i < n_cand; i++) {
        if (g_file_get_contents(candidates[i], (gchar **)&data, &length, NULL)) {
            if (length == 112640) {
                break;
            }
            g_free(data);
            data = NULL;
        }
    }

    g_free(home_path);

    if (data) {
        *out_size = (size_t)length;
    }
    return data;
}

static void free_cached_tile(gpointer data)
{
    if (data) {
        UvCachedTile *tile = (UvCachedTile *)data;
        g_free(tile->pixels);
        g_free(tile);
    }
}

UvScreenRenderer *uv_screen_renderer_new(const UvScreenInit *init, const char *custom_dict_path)
{
    if (!init || init->width <= 0 || init->height <= 0 ||
        init->width > UV_MAX_SCREEN_DIM || init->height > UV_MAX_SCREEN_DIM) {
        return NULL;
    }

    UvScreenRenderer *r = g_new0(UvScreenRenderer, 1);
    r->width = init->width;
    r->height = init->height;
    r->tile_size = init->tile_size > 0 ? init->tile_size : 16;
    r->last_frame_number = UV_RESET_FRAME_NUMBER;
    r->virtual_origin_x = init->virtual_origin_x;
    r->virtual_origin_y = init->virtual_origin_y;
    g_strlcpy(r->host_version, init->version, sizeof(r->host_version));

    g_mutex_init(&r->mutex);

    /* Allocate Cairo surface and direct pixel buffer */
    r->cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, r->width, r->height);
    r->main_pixels = (uint32_t *)cairo_image_surface_get_data(r->cairo_surface);
    r->main_stride = (size_t)cairo_image_surface_get_stride(r->cairo_surface);
    memset(r->main_pixels, 0, r->main_stride * (size_t)r->height);

    /* Allocate bottom-up RGB555 temp buffer */
    r->temp_stride = ((size_t)r->width * 2 + 3) & ~3;
    r->temp_pixels = g_malloc0(r->temp_stride * (size_t)r->height);

    /* Tile cache setup */
    r->cache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free_cached_tile);
    r->cache_order = g_queue_new();

    /* Load Zstd dictionary and create DCtx */
    r->dict_data = load_screen_dictionary(custom_dict_path, &r->dict_size);
    r->zstd_dctx = ZSTD_createDCtx();
    if (r->dict_data && r->dict_size > 0) {
        ZSTD_DCtx_loadDictionary(r->zstd_dctx, r->dict_data, r->dict_size);
    }

    return r;
}

void uv_screen_renderer_free(UvScreenRenderer *r)
{
    if (!r) return;

    if (r->zstd_dctx) {
        ZSTD_freeDCtx(r->zstd_dctx);
    }
    g_free(r->dict_data);

    if (r->cairo_surface) {
        cairo_surface_destroy(r->cairo_surface);
    }
    g_free(r->temp_pixels);

    if (r->cache) {
        g_hash_table_destroy(r->cache);
    }
    if (r->cache_order) {
        g_queue_free(r->cache_order);
    }

    g_mutex_clear(&r->mutex);
    g_free(r);
}

bool uv_parse_screen_init(const GArray *parts, UvScreenInit *out_init, char **err_msg)
{
    if (!parts || parts->len < 3 || !out_init) {
        if (err_msg) *err_msg = g_strdup("Screen init message contains too few parts");
        return false;
    }
    memset(out_init, 0, sizeof(*out_init));

    const UvPart *p0 = &g_array_index(parts, UvPart, 0);
    if (p0->len < 1 || p0->data[0] != 0x00) {
        if (err_msg) *err_msg = g_strdup("Invalid screen init command identifier");
        return false;
    }

    const UvPart *p1 = &g_array_index(parts, UvPart, 1);
    const UvPart *p2 = &g_array_index(parts, UvPart, 2);
    if (p1->len < 4 || p2->len < 4) {
        if (err_msg) *err_msg = g_strdup("Screen init missing width or height part");
        return false;
    }

    out_init->width = uv_read_i32_le(p1->data);
    out_init->height = uv_read_i32_le(p2->data);
    out_init->tile_size = 16;

    if (parts->len > 6) {
        const UvPart *p6 = &g_array_index(parts, UvPart, 6);
        if (p6->len > 0) {
            size_t vlen = p6->len < sizeof(out_init->version) - 1 ? p6->len : sizeof(out_init->version) - 1;
            memcpy(out_init->version, p6->data, vlen);
            out_init->version[vlen] = '\0';
        }
    }

    if (parts->len > 9) {
        const UvPart *p9 = &g_array_index(parts, UvPart, 9);
        if (p9->len >= 4) out_init->virtual_origin_x = uv_read_i32_le(p9->data);
    }
    if (parts->len > 10) {
        const UvPart *p10 = &g_array_index(parts, UvPart, 10);
        if (p10->len >= 4) out_init->virtual_origin_y = uv_read_i32_le(p10->data);
    }
    if (parts->len > 14) {
        const UvPart *p14 = &g_array_index(parts, UvPart, 14);
        if (p14->len >= 1 && p14->data[0] > 0) {
            out_init->tile_size = (int)p14->data[0];
        }
    }

    if (out_init->width <= 0 || out_init->height <= 0 ||
        out_init->width > UV_MAX_SCREEN_DIM || out_init->height > UV_MAX_SCREEN_DIM ||
        ((int64_t)out_init->width * out_init->height) > UV_MAX_SCREEN_PIXELS) {
        if (err_msg) *err_msg = g_strdup_printf("Invalid remote screen dimensions: %dx%d", out_init->width, out_init->height);
        return false;
    }

    return true;
}

static void rect_buffer_copy(UvScreenRenderer *r, const uint8_t *source,
                             int sw, int sh, int64_t x, int64_t y)
{
    if (sw <= 0 || sh <= 0) return;
    int64_t mw = r->width;
    int64_t mh = r->height;

    if (x >= mw || y >= mh || x <= -sw || y <= -sh) return;

    int sx = (x < 0) ? (int)(-x) : 0;
    int sy = (y < 0) ? (int)(-y) : 0;
    int dx = (x > 0) ? (int)x : 0;
    int dy = (y > 0) ? (int)y : 0;

    int cw = (sw - sx < r->width - dx) ? (sw - sx) : (r->width - dx);
    int ch = (sh - sy < r->height - dy) ? (sh - sy) : (r->height - dy);
    if (cw <= 0 || ch <= 0) return;

    size_t copy_bytes = (size_t)cw * 2;
    size_t src_stride = (size_t)sw * 2;

    for (int row = 0; row < ch; row++) {
        size_t s_off = (size_t)(sy + row) * src_stride + (size_t)sx * 2;
        /* Bottom-up destination: row (dy + row) maps to (height - 1 - (dy + row)) */
        size_t d_row = (size_t)(r->height - 1 - (dy + row));
        size_t d_off = d_row * r->temp_stride + (size_t)dx * 2;
        memcpy(r->temp_pixels + d_off, source + s_off, copy_bytes);
    }
}

static void temp_to_main(UvScreenRenderer *r, int64_t temp_x, int64_t temp_y, const UvRect *rect)
{
    int64_t mw = r->width;
    int64_t mh = r->height;
    int64_t rx = rect->x;
    int64_t ry = rect->y;
    int64_t rw = rect->width;
    int64_t rh = rect->height;

    int64_t left = 0;
    if (-temp_x > left) left = -temp_x;
    if (-rx > left) left = -rx;

    int64_t top = 0;
    if (-temp_y > top) top = -temp_y;
    if (-ry > top) top = -ry;

    int64_t right = rw;
    if (mw - temp_x < right) right = mw - temp_x;
    if (mw - rx < right) right = mw - rx;

    int64_t bottom = rh;
    if (mh - temp_y < bottom) bottom = mh - temp_y;
    if (mh - ry < bottom) bottom = mh - ry;

    if (left >= right || top >= bottom) return;

    int sx = (int)(temp_x + left);
    int sy = (int)(temp_y + top);
    int dx = (int)(rx + left);
    int dy = (int)(ry + top);
    int w = (int)(right - left);
    int h = (int)(bottom - top);

    for (int row = 0; row < h; row++) {
        size_t s_row = (size_t)(r->height - 1 - (sy + row));
        const uint16_t *src = (const uint16_t *)(r->temp_pixels + s_row * r->temp_stride) + sx;
        uint32_t *dst = r->main_pixels + (size_t)(dy + row) * (r->main_stride / 4) + dx;
        for (int col = 0; col < w; col++) {
            dst[col] = rgb555_to_xrgb8888(src[col]);
        }
    }
}

static void main_to_temp(UvScreenRenderer *r, const UvRect *rect, int64_t temp_x, int64_t temp_y)
{
    int64_t mw = r->width;
    int64_t mh = r->height;
    int64_t rx = rect->x;
    int64_t ry = rect->y;
    int64_t rw = rect->width;
    int64_t rh = rect->height;

    int64_t left = 0;
    if (-rx > left) left = -rx;
    if (-temp_x > left) left = -temp_x;

    int64_t top = 0;
    if (-ry > top) top = -ry;
    if (-temp_y > top) top = -temp_y;

    int64_t right = rw;
    if (mw - rx < right) right = mw - rx;
    if (mw - temp_x < right) right = mw - temp_x;

    int64_t bottom = rh;
    if (mh - ry < bottom) bottom = mh - ry;
    if (mh - temp_y < bottom) bottom = mh - temp_y;

    if (left >= right || top >= bottom) return;

    int sx = (int)(rx + left);
    int sy = (int)(ry + top);
    int dx = (int)(temp_x + left);
    int dy = (int)(temp_y + top);
    int w = (int)(right - left);
    int h = (int)(bottom - top);

    for (int row = 0; row < h; row++) {
        const uint32_t *src = r->main_pixels + (size_t)(sy + row) * (r->main_stride / 4) + sx;
        size_t d_row = (size_t)(r->height - 1 - (dy + row));
        uint16_t *dst = (uint16_t *)(r->temp_pixels + d_row * r->temp_stride) + dx;
        for (int col = 0; col < w; col++) {
            dst[col] = xrgb8888_to_rgb555(src[col]);
        }
    }
}

static uint8_t *extract_temp_rect(UvScreenRenderer *r, const UvRect *rect, size_t *out_len)
{
    int w = (int)rect->width;
    int h = (int)rect->height;
    size_t size = (size_t)w * (size_t)h * 2;
    uint8_t *buf = g_malloc(size);

    for (int row = 0; row < h; row++) {
        size_t s_row = (size_t)(r->height - 1 - (rect->y + row));
        const uint8_t *src = r->temp_pixels + s_row * r->temp_stride + (size_t)rect->x * 2;
        memcpy(buf + (size_t)row * ((size_t)w * 2), src, (size_t)w * 2);
    }

    *out_len = size;
    return buf;
}

/* JPEG Decompressor Helper */
typedef struct {
    int width;
    int height;
    uint32_t *pixels;
} UvAtlas;

static bool decode_jpeg_atlas(const uint8_t *data, size_t len, UvAtlas *atlas)
{
    if (!data || len < 2 || data[0] != 0xFF || data[1] != 0xD8) return false;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data, (unsigned long)len);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int w = (int)cinfo.output_width;
    int h = (int)cinfo.output_height;
    uint32_t *pixels = g_malloc((size_t)w * (size_t)h * 4);

    JSAMPARRAY row_buf = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, (JDIMENSION)w * 3, 1);

    while (cinfo.output_scanline < cinfo.output_height) {
        int y = (int)cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, row_buf, 1);
        const uint8_t *src = row_buf[0];
        uint32_t *dst = pixels + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            uint32_t r = src[x * 3];
            uint32_t g = src[x * 3 + 1];
            uint32_t b = src[x * 3 + 2];
            dst[x] = (r << 16) | (g << 8) | b;
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    atlas->width = w;
    atlas->height = h;
    atlas->pixels = pixels;
    return true;
}

bool uv_screen_apply_frame(UvScreenRenderer *r, const GArray *parts, char **err_msg)
{
    if (!r || !parts || parts->len <= 10) {
        if (err_msg) *err_msg = g_strdup("Screen frame message missing required parts");
        return false;
    }

    const UvPart *p0 = &g_array_index(parts, UvPart, 0);
    if (p0->len < 1 || p0->data[0] != 0x01) {
        if (err_msg) *err_msg = g_strdup("Invalid screen frame command ID");
        return false;
    }

    /* Frame sequence validation */
    const UvPart *p4 = &g_array_index(parts, UvPart, 4);
    if (p4->len < 4) {
        if (err_msg) *err_msg = g_strdup("Missing frame sequence number");
        return false;
    }
    int32_t frame_num = uv_read_i32_le(p4->data);
    if (r->last_frame_number == UV_RESET_FRAME_NUMBER) {
        r->last_frame_number = frame_num - 1;
    }
    int32_t expected = r->last_frame_number + 1;
    if (frame_num != expected) {
        /* On sequence jump or resolution switch, resynchronize to avoid stalling stream */
        r->last_frame_number = frame_num - 1;
    }

    /* Part 1: Main rectangles */
    const UvPart *p1 = &g_array_index(parts, UvPart, 1);
    if (p1->len % 8 != 0) {
        if (err_msg) *err_msg = g_strdup("Malformed main rectangles part");
        return false;
    }
    size_t n_main = p1->len / 8;
    UvRect *main_rects = g_new0(UvRect, n_main);
    size_t total_decompressed_bytes = 0;

    for (size_t i = 0; i < n_main; i++) {
        const uint8_t *rec = p1->data + i * 8;
        main_rects[i].x = uv_read_u16_le(rec);
        main_rects[i].y = uv_read_u16_le(rec + 2);
        main_rects[i].width = uv_read_u16_le(rec + 4);
        main_rects[i].height = uv_read_u16_le(rec + 6);
        total_decompressed_bytes += (size_t)main_rects[i].width * (size_t)main_rects[i].height * 2;
    }

    /* Part 2: Zstd compressed pixels */
    const UvPart *p2 = &g_array_index(parts, UvPart, 2);
    uint8_t *raw_pixels = NULL;

    if (total_decompressed_bytes > 0) {
        if (!r->dict_data) {
            g_free(main_rects);
            if (err_msg) *err_msg = g_strdup("8bit.dat dictionary required for screen decompression");
            return false;
        }

        raw_pixels = g_malloc(total_decompressed_bytes);
        ZSTD_DCtx_reset(r->zstd_dctx, ZSTD_reset_session_only);

        ZSTD_inBuffer in_buf = { p2->data, p2->len, 0 };
        ZSTD_outBuffer out_buf = { raw_pixels, total_decompressed_bytes, 0 };

        size_t ret = ZSTD_decompressStream(r->zstd_dctx, &out_buf, &in_buf);
        if (ZSTD_isError(ret) || out_buf.pos != total_decompressed_bytes) {
            g_free(main_rects);
            g_free(raw_pixels);
            if (err_msg) *err_msg = g_strdup("Zstandard decompression length mismatch or error");
            return false;
        }
    }

    g_mutex_lock(&r->mutex);

    /* 1. Scroll */
    const UvPart *p6 = &g_array_index(parts, UvPart, 6);
    if (p6->len >= 10 && (p6->len - 10) % 8 == 0) {
        int16_t v_delta = uv_read_i16_le(p6->data);
        UvRect capture;
        capture.x = uv_read_i16_le(p6->data + 2);
        capture.y = uv_read_i16_le(p6->data + 4);
        capture.width = uv_read_i16_le(p6->data + 6);
        capture.height = uv_read_i16_le(p6->data + 8);

        main_to_temp(r, &capture, capture.x, capture.y - v_delta);

        size_t n_copyback = (p6->len - 10) / 8;
        for (size_t i = 0; i < n_copyback; i++) {
            const uint8_t *rec = p6->data + 10 + i * 8;
            UvRect cb;
            cb.x = uv_read_i16_le(rec);
            cb.y = uv_read_i16_le(rec + 2);
            cb.width = uv_read_i16_le(rec + 4);
            cb.height = uv_read_i16_le(rec + 6);
            temp_to_main(r, cb.x, cb.y, &cb);
        }
    }

    /* 2. Cache references (write to temp) */
    const UvPart *p9 = &g_array_index(parts, UvPart, 9);
    if (p9->len % 8 == 0) {
        size_t n_refs = p9->len / 8;
        for (size_t i = 0; i < n_refs; i++) {
            const uint8_t *rec = p9->data + i * 8;
            int16_t x = uv_read_i16_le(rec);
            int16_t y = uv_read_i16_le(rec + 2);
            int32_t hash = uv_read_i32_le(rec + 4);

            UvCachedTile *tile = g_hash_table_lookup(r->cache, GINT_TO_POINTER(hash));
            if (tile) {
                rect_buffer_copy(r, tile->pixels, tile->width, tile->height, x, y);
                g_queue_remove(r->cache_order, GINT_TO_POINTER(hash));
                g_queue_push_tail(r->cache_order, GINT_TO_POINTER(hash));
            }
        }
    }

    /* 3. Copy-from-temporary rectangles */
    const UvPart *p5 = &g_array_index(parts, UvPart, 5);
    if (p5->len % 8 == 0) {
        size_t n_copy = p5->len / 8;
        for (size_t i = 0; i < n_copy; i++) {
            const uint8_t *rec = p5->data + i * 8;
            UvRect rect;
            rect.x = uv_read_i16_le(rec);
            rect.y = uv_read_i16_le(rec + 2);
            rect.width = uv_read_i16_le(rec + 4);
            rect.height = uv_read_i16_le(rec + 6);
            temp_to_main(r, rect.x, rect.y, &rect);
        }
    }

    /* 4. JPEG atlas tiles */
    const UvPart *p7 = &g_array_index(parts, UvPart, 7);
    const UvPart *p8 = &g_array_index(parts, UvPart, 8);
    if (p7->len > 0 && p8->len % 8 == 0 && p8->len > 0) {
        UvAtlas atlas;
        if (decode_jpeg_atlas(p7->data, p7->len, &atlas)) {
            size_t n_aux = p8->len / 8;
            int sy_col = 0;
            int sx_col = 0;
            int tile_sz = r->tile_size;

            for (size_t i = 0; i < n_aux; i++) {
                const uint8_t *rec = p8->data + i * 8;
                int16_t ax = uv_read_i16_le(rec);
                int16_t ay = uv_read_i16_le(rec + 2);
                int16_t aw = uv_read_i16_le(rec + 4);
                int16_t ah = uv_read_i16_le(rec + 6);

                for (int tx = 0; tx < aw; tx += tile_sz) {
                    for (int ty = 0; ty < ah; ty += tile_sz) {
                        int cw = (aw - tx < tile_sz) ? (aw - tx) : tile_sz;
                        int ch = (ah - ty < tile_sz) ? (ah - ty) : tile_sz;

                        if (sy_col + ch > atlas.height) {
                            sx_col += tile_sz;
                            sy_col = 0;
                        }

                        int dest_x = ax + tx;
                        int dest_y = ay + ty;

                        if (dest_x < r->width && dest_y < r->height && sx_col + cw <= atlas.width) {
                            for (int row = 0; row < ch; row++) {
                                int s_row = atlas.height - 1 - sy_col - row;
                                if (s_row >= 0 && s_row < atlas.height && dest_y + row < r->height) {
                                    const uint32_t *src_line = atlas.pixels + (size_t)s_row * (size_t)atlas.width + sx_col;
                                    uint32_t *dst_line = r->main_pixels + (size_t)(dest_y + row) * (r->main_stride / 4) + dest_x;
                                    int copy_w = (dest_x + cw <= r->width) ? cw : (r->width - dest_x);
                                    memcpy(dst_line, src_line, (size_t)copy_w * 4);
                                }
                            }
                        }
                        sy_col += ch;
                    }
                }
            }
            g_free(atlas.pixels);
        }
    }

    /* 5. Main raw rectangles */
    size_t raw_offset = 0;
    for (size_t i = 0; i < n_main; i++) {
        size_t n_bytes = (size_t)main_rects[i].width * (size_t)main_rects[i].height * 2;
        rect_buffer_copy(r, raw_pixels + raw_offset, (int)main_rects[i].width, (int)main_rects[i].height, 0, 0);
        temp_to_main(r, 0, 0, &main_rects[i]);
        raw_offset += n_bytes;
    }

    /* 6. Cache additions */
    const UvPart *p10 = &g_array_index(parts, UvPart, 10);
    if (p10->len % 12 == 0) {
        size_t n_adds = p10->len / 12;
        for (size_t i = 0; i < n_adds; i++) {
            const uint8_t *rec = p10->data + i * 12;
            UvRect rect;
            rect.x = uv_read_i16_le(rec);
            rect.y = uv_read_i16_le(rec + 2);
            rect.width = uv_read_i16_le(rec + 4);
            rect.height = uv_read_i16_le(rec + 6);
            int32_t hash = uv_read_i32_le(rec + 8);

            main_to_temp(r, &rect, rect.x, rect.y);

            if (!g_hash_table_contains(r->cache, GINT_TO_POINTER(hash))) {
                size_t tile_len = 0;
                uint8_t *tile_pixels = extract_temp_rect(r, &rect, &tile_len);
                UvCachedTile *tile = g_new0(UvCachedTile, 1);
                tile->width = (int)rect.width;
                tile->height = (int)rect.height;
                tile->pixels = tile_pixels;

                g_hash_table_insert(r->cache, GINT_TO_POINTER(hash), tile);
                g_queue_push_tail(r->cache_order, GINT_TO_POINTER(hash));
                r->cache_bytes += tile_len;
            }
        }
    }

    /* Evict oldest cache entries if limit exceeded */
    while (g_queue_get_length(r->cache_order) > UV_MAX_CACHE_ENTRIES) {
        gpointer oldest_hash = g_queue_pop_head(r->cache_order);
        UvCachedTile *tile = g_hash_table_lookup(r->cache, oldest_hash);
        if (tile) {
            r->cache_bytes -= (size_t)tile->width * (size_t)tile->height * 2;
            g_hash_table_remove(r->cache, oldest_hash);
        }
    }

    r->last_frame_number = frame_num;
    cairo_surface_mark_dirty(r->cairo_surface);

    g_mutex_unlock(&r->mutex);

    g_free(main_rects);
    g_free(raw_pixels);
    return true;
}

bool uv_screen_set_tile_size(UvScreenRenderer *r, const GArray *parts)
{
    if (!r || !parts || parts->len < 2) return false;
    const UvPart *p1 = &g_array_index(parts, UvPart, 1);
    if (p1->len < 1 || p1->data[0] == 0) return false;

    g_mutex_lock(&r->mutex);
    r->tile_size = (int)p1->data[0];
    g_mutex_unlock(&r->mutex);
    return true;
}
