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

#ifndef UV_CURSOR_H
#define UV_CURSOR_H

#include "uv_types.h"
#include <gdk/gdk.h>

typedef struct {
    GHashTable *cache;       /* hash (int32) -> GdkCursor* */
    GdkCursor *current_cursor;
    int32_t current_hash;
    int16_t remote_x;
    int16_t remote_y;
    GMutex mutex;
} UvCursorManager;

UvCursorManager *uv_cursor_manager_new(void);
void uv_cursor_manager_free(UvCursorManager *mgr);

bool uv_cursor_handle_selection(UvCursorManager *mgr, const GArray *parts, GdkCursor **out_cursor);

bool uv_cursor_handle_shape_transfer(UvCursorManager *mgr, const GArray *parts, GdkCursor **out_cursor);

bool uv_cursor_handle_position(UvCursorManager *mgr, const GArray *parts, int16_t *out_x, int16_t *out_y);

#endif /* UV_CURSOR_H */
