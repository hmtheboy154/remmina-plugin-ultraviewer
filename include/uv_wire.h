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

#ifndef UV_WIRE_H
#define UV_WIRE_H

#include "uv_types.h"

typedef enum {
    UV_ENVELOPE_UNKNOWN = 0,
    UV_ENVELOPE_FORWARD,
    UV_ENVELOPE_SYSTEM
} UvEnvelopeType;

typedef struct {
    UvEnvelopeType type;
    bool format_flag;
    char ip[16];
    uint16_t port;
    const uint8_t *payload;
    size_t payload_len;
} UvEnvelope;

typedef struct {
    const uint8_t *data;
    size_t len;
} UvPart;

bool uv_frame_encode(const uint8_t *payload, size_t len,
                     uint8_t **out_frame, size_t *out_len);

bool uv_system_envelope_encode(const uint8_t *payload, size_t len, bool with_flag,
                               uint8_t **out_env, size_t *out_len);

bool uv_forward_envelope_encode(const char *ip_str, uint16_t port, bool format_flag,
                                const uint8_t *payload, size_t len,
                                uint8_t **out_env, size_t *out_len);

bool uv_envelope_decode(const uint8_t *buf, size_t len, UvEnvelope *out_env);

bool uv_multipart_encode(const UvPart *parts, size_t n_parts,
                         uint8_t **out_buf, size_t *out_len);

bool uv_multipart_decode(const uint8_t *buf, size_t len,
                         GArray **out_parts);

char *uv_http_preamble_create(const char *host);

char *uv_join_fields(const char *const *fields, size_t n_fields);
char **uv_split_fields(const char *str, char delimiter, int max_tokens);

#endif /* UV_WIRE_H */
