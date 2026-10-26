/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - TLS Transport & Sockets
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UV_TLS_H
#define UV_TLS_H

#include "uv_types.h"
#include <openssl/ssl.h>

typedef struct {
    int fd;
    SSL_CTX *ctx;
    SSL *ssl;
    char *hostname;
    uint16_t port;
    bool insecure_tls;
    uint8_t read_buf[65536];
    size_t read_buf_len;
    size_t read_buf_pos;
} UvTlsSocket;

UvTlsSocket *uv_tls_connect(const char *host, uint16_t port,
                            const char *expected_hostname,
                            bool insecure_tls, char **err_msg);

void uv_tls_close(UvTlsSocket *sock);

int uv_tls_get_fd(UvTlsSocket *sock);

bool uv_tls_write_frame(UvTlsSocket *sock, const uint8_t *payload, size_t len);

bool uv_tls_read_frame(UvTlsSocket *sock, uint8_t **out_payload, size_t *out_len,
                       int timeout_ms, char **err_msg);

#endif /* UV_TLS_H */
