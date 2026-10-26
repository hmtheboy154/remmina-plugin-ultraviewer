/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Input Encoding
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UV_INPUT_H
#define UV_INPUT_H

#include "uv_types.h"
#include <gdk/gdk.h>

#define UV_WM_MOUSEMOVE     512
#define UV_WM_LBUTTONDOWN   513
#define UV_WM_LBUTTONUP     514
#define UV_WM_RBUTTONDOWN   516
#define UV_WM_RBUTTONUP     517
#define UV_WM_MBUTTONDOWN   519
#define UV_WM_MBUTTONUP     520
#define UV_WM_MOUSEWHEEL    522

#define UV_KEY_DOWN         1
#define UV_KEY_UP           2
#define UV_SYS_KEY_DOWN     3
#define UV_SYS_KEY_UP       4
#define UV_VK_PACKET        0xE7

bool uv_encode_mouse_move(int x, int y, uint16_t flags,
                          uint8_t **out_payload, size_t *out_len);

bool uv_encode_mouse_button(uint32_t msg, int x, int y, uint16_t flags,
                            uint8_t **out_payload, size_t *out_len);

bool uv_encode_mouse_wheel(int16_t delta, int x, int y, uint16_t flags,
                           uint8_t **out_payload, size_t *out_len);

bool uv_encode_key(guint keyval, GdkModifierType state, bool is_press,
                   uint8_t **out_payload, size_t *out_len);

bool uv_encode_unicode_char(uint16_t ch, bool is_press, bool caps_lock,
                            uint8_t **out_payload, size_t *out_len);

#endif /* UV_INPUT_H */
