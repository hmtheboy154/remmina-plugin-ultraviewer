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

#ifndef UV_SCREEN_H
#define UV_SCREEN_H

#include "uv_types.h"
#include <cairo.h>
#include <zstd.h>
#include <jpeglib.h>

typedef struct {
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
} UvRect;

typedef struct {
    int width;
    int height;
    int tile_size;
    int virtual_origin_x;
    int virtual_origin_y;
    char version[32];
} UvScreenInit;

typedef struct {
    int width;
    int height;
    uint8_t *pixels;
} UvCachedTile;

typedef struct {
    int width;
    int height;
    int tile_size;
    int32_t last_frame_number;
    int virtual_origin_x;
    int virtual_origin_y;
    char host_version[32];

    /* Framebuffers */
    cairo_surface_t *cairo_surface;
    uint32_t *main_pixels;   /* 0x00RRGGBB top-down */
    size_t main_stride;      /* in bytes */

    uint8_t *temp_pixels;    /* bottom-up RGB555 */
    size_t temp_stride;      /* DWORD aligned: (width * 2 + 3) & ~3 */

    /* Tile cache: hash -> UvCachedTile* */
    GHashTable *cache;
    GQueue *cache_order;     /* LRU order */
    size_t cache_bytes;

    /* Zstandard decoder */
    ZSTD_DCtx *zstd_dctx;
    uint8_t *dict_data;
    size_t dict_size;

    GMutex mutex;
} UvScreenRenderer;

UvScreenRenderer *uv_screen_renderer_new(const UvScreenInit *init, const char *custom_dict_path);
void uv_screen_renderer_free(UvScreenRenderer *renderer);

bool uv_parse_screen_init(const GArray *parts, UvScreenInit *out_init, char **err_msg);

bool uv_screen_apply_frame(UvScreenRenderer *r, const GArray *parts, char **err_msg);

bool uv_screen_set_tile_size(UvScreenRenderer *r, const GArray *parts);

#endif /* UV_SCREEN_H */
