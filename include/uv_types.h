/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UV_TYPES_H
#define UV_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>

#define UV_MAX_FRAME_SIZE       (16 * 1024 * 1024)   /* 16 MiB */
#define UV_MAX_MULTIPART_PARTS  4096
#define UV_MAX_MULTIPART_BYTES  (64 * 1024 * 1024)   /* 64 MiB */
#define UV_MAX_SCREEN_DIM       16384
#define UV_MAX_SCREEN_PIXELS    32000000
#define UV_MAX_ZSTD_OUTPUT      (128 * 1024 * 1024)  /* 128 MiB */
#define UV_MAX_CACHE_ENTRIES    1000
#define UV_MAX_CACHE_BYTES      (64 * 1024 * 1024)   /* 64 MiB */

#define UV_RESET_FRAME_NUMBER   (-2147483647)        /* 0x80000001 */
#define UV_IGNORED_SELECTION_HASH ((int32_t)0xb84d1a70)

#define UV_DEFAULT_COMMAND_HOST "functions3.ultraviewer.net"
#define UV_DEFAULT_PORT         443
#define UV_SNI_LABEL            "ultraviewer"
#define UV_CLIENT_VERSION       "6.6.133"

static inline uint16_t uv_read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline int16_t uv_read_i16_le(const uint8_t *p)
{
    return (int16_t)uv_read_u16_le(p);
}

static inline uint32_t uv_read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline int32_t uv_read_i32_le(const uint8_t *p)
{
    return (int32_t)uv_read_u32_le(p);
}

static inline void uv_write_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void uv_write_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline uint64_t uv_read_u64_le(const uint8_t *p)
{
    return ((uint64_t)p[0]) |
           (((uint64_t)p[1]) << 8) |
           (((uint64_t)p[2]) << 16) |
           (((uint64_t)p[3]) << 24) |
           (((uint64_t)p[4]) << 32) |
           (((uint64_t)p[5]) << 40) |
           (((uint64_t)p[6]) << 48) |
           (((uint64_t)p[7]) << 56);
}

static inline double uv_read_f64_le(const uint8_t *p)
{
    union {
        uint64_t u;
        double f;
    } val;
    val.u = uv_read_u64_le(p);
    return val.f;
}

static inline void uv_write_u64_le(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
    }
}

#endif /* UV_TYPES_H */
