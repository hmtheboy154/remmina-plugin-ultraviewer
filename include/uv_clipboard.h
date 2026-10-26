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

#ifndef UV_CLIPBOARD_H
#define UV_CLIPBOARD_H

#include "uv_types.h"

bool uv_clipboard_encode_viewer_to_host(const char *text,
                                        uint8_t **out_payload,
                                        size_t *out_len,
                                        bool *out_format_flag);

char *uv_clipboard_decode_host_to_viewer(const GArray *parts);

char *uv_clipboard_decode_legacy(const uint8_t *payload, size_t len);

#endif /* UV_CLIPBOARD_H */
