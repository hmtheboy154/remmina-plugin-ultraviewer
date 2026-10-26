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

#include "uv_tls.h"
#include "uv_wire.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>

/* Load trusted Sectigo CA certificates from file bundle or system store */
static void uv_tls_load_ca_certificates(SSL_CTX *ctx)
{
    /* Always load system default verify paths (e.g. /etc/ssl/certs) */
    SSL_CTX_set_default_verify_paths(ctx);

    const char *candidates[8];
    int n_cand = 0;

    char *home_cert = g_build_filename(g_get_user_config_dir(), "remmina", "ultraviewer", "sectigo_roots.pem", NULL);

    candidates[n_cand++] = "/usr/share/remmina/ultraviewer/sectigo_roots.pem";
    candidates[n_cand++] = "/usr/lib/remmina/plugins/sectigo_roots.pem";
    candidates[n_cand++] = home_cert;
    candidates[n_cand++] = "certs/sectigo_roots.pem";
    candidates[n_cand++] = "../certs/sectigo_roots.pem";
    candidates[n_cand++] = "../../certs/sectigo_roots.pem";

    for (int i = 0; i < n_cand; i++) {
        if (!candidates[i]) continue;
        if (g_file_test(candidates[i], G_FILE_TEST_EXISTS)) {
            if (SSL_CTX_load_verify_locations(ctx, candidates[i], NULL) == 1) {
                break;
            }
        }
    }

    g_free(home_cert);
}

static int connect_tcp_with_timeout(const char *host, uint16_t port, int timeout_ms)
{
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        int rc = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0) {
            fcntl(fd, F_SETFL, flags);
            break;
        }

        if (errno == EINPROGRESS) {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            int prc = poll(&pfd, 1, timeout_ms);
            if (prc > 0 && (pfd.revents & POLLOUT)) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                    fcntl(fd, F_SETFL, flags);
                    break;
                }
            }
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

static int uv_tls_verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
    if (!preverify_ok) {
        int err = X509_STORE_CTX_get_error(ctx);
        /* UltraViewer command servers and relays intermittently serve expired certificates
         * which the official Windows client and MacViewer explicitly accept. */
        if (err == X509_V_ERR_CERT_HAS_EXPIRED || err == X509_V_ERR_CERT_NOT_YET_VALID) {
            return 1;
        }
    }
    return preverify_ok;
}

UvTlsSocket *uv_tls_connect(const char *host, uint16_t port,
                            const char *expected_hostname,
                            bool insecure_tls, char **err_msg)
{
    int fd = connect_tcp_with_timeout(host, port, 3000);
    if (fd < 0) {
        if (err_msg) *err_msg = g_strdup_printf("Could not connect to TCP %s:%u", host, port);
        return NULL;
    }

    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        close(fd);
        if (err_msg) *err_msg = g_strdup("Failed to initialize OpenSSL context");
        return NULL;
    }

    if (insecure_tls) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    } else {
        uv_tls_load_ca_certificates(ctx);
        X509_VERIFY_PARAM *param = SSL_CTX_get0_param(ctx);
        X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_NO_CHECK_TIME);
        if (expected_hostname && *expected_hostname) {
            X509_VERIFY_PARAM_set1_host(param, expected_hostname, 0);
        }
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, uv_tls_verify_callback);
    }

    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        SSL_CTX_free(ctx);
        close(fd);
        if (err_msg) *err_msg = g_strdup("Failed to create SSL handle");
        return NULL;
    }

    /* Crucial UltraViewer protocol rule: SNI is literal "ultraviewer" */
    SSL_set_tlsext_host_name(ssl, UV_SNI_LABEL);
    SSL_set_fd(ssl, fd);

    if (SSL_connect(ssl) != 1) {
        long v_res = SSL_get_verify_result(ssl);
        if (err_msg) {
            *err_msg = g_strdup_printf("TLS handshake failed: %s",
                                       X509_verify_cert_error_string(v_res));
        }
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return NULL;
    }

    /* Send HTTP tunnel preamble */
    char *preamble = uv_http_preamble_create(expected_hostname ? expected_hostname : host);
    size_t preamble_len = strlen(preamble);
    int written = SSL_write(ssl, preamble, (int)preamble_len);
    g_free(preamble);

    if (written <= 0) {
        if (err_msg) *err_msg = g_strdup("Failed to send HTTP preamble");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return NULL;
    }

    /* Consume HTTP response header up to \r\n\r\n */
    UvTlsSocket *sock = g_new0(UvTlsSocket, 1);
    sock->fd = fd;
    sock->ctx = ctx;
    sock->ssl = ssl;
    sock->hostname = g_strdup(host);
    sock->port = port;
    sock->insecure_tls = insecure_tls;

    uint8_t hdr_buf[32768];
    size_t hdr_len = 0;
    bool found_delimiter = false;

    while (hdr_len < sizeof(hdr_buf) - 1) {
        int r = SSL_read(ssl, hdr_buf + hdr_len, (int)(sizeof(hdr_buf) - hdr_len));
        if (r <= 0) {
            break;
        }
        hdr_len += r;
        hdr_buf[hdr_len] = '\0';

        char *delim = strstr((char *)hdr_buf, "\r\n\r\n");
        if (delim) {
            size_t delim_pos = (size_t)(delim - (char *)hdr_buf) + 4;
            size_t leftover = hdr_len - delim_pos;
            if (leftover > 0) {
                memcpy(sock->read_buf, hdr_buf + delim_pos, leftover);
                sock->read_buf_len = leftover;
            }
            found_delimiter = true;
            break;
        }
    }

    if (!found_delimiter) {
        if (err_msg) *err_msg = g_strdup("Server did not return valid HTTP preamble response");
        uv_tls_close(sock);
        return NULL;
    }

    return sock;
}

void uv_tls_close(UvTlsSocket *sock)
{
    if (!sock) return;
    if (sock->ssl) {
        SSL_shutdown(sock->ssl);
        SSL_free(sock->ssl);
        sock->ssl = NULL;
    }
    if (sock->ctx) {
        SSL_CTX_free(sock->ctx);
        sock->ctx = NULL;
    }
    if (sock->fd >= 0) {
        close(sock->fd);
        sock->fd = -1;
    }
    g_free(sock->hostname);
    g_free(sock);
}

int uv_tls_get_fd(UvTlsSocket *sock)
{
    return sock ? sock->fd : -1;
}

static bool read_exact(UvTlsSocket *sock, uint8_t *dest, size_t count, int timeout_ms)
{
    size_t filled = 0;

    /* Drain buffered bytes first */
    if (sock->read_buf_len > sock->read_buf_pos) {
        size_t avail = sock->read_buf_len - sock->read_buf_pos;
        size_t take = avail < count ? avail : count;
        memcpy(dest, sock->read_buf + sock->read_buf_pos, take);
        sock->read_buf_pos += take;
        filled += take;
        if (sock->read_buf_pos == sock->read_buf_len) {
            sock->read_buf_pos = 0;
            sock->read_buf_len = 0;
        }
    }

    while (filled < count) {
        if (SSL_pending(sock->ssl) == 0 && timeout_ms >= 0) {
            struct pollfd pfd;
            pfd.fd = sock->fd;
            pfd.events = POLLIN;
            int prc = poll(&pfd, 1, timeout_ms);
            if (prc <= 0) return false;
        }

        int r = SSL_read(sock->ssl, dest + filled, (int)(count - filled));
        if (r <= 0) return false;
        filled += (size_t)r;
    }

    return true;
}

bool uv_tls_read_frame(UvTlsSocket *sock, uint8_t **out_payload, size_t *out_len,
                       int timeout_ms, char **err_msg)
{
    if (!sock || !sock->ssl) return false;

    uint8_t len_bytes[4];
    if (!read_exact(sock, len_bytes, 4, timeout_ms)) {
        if (err_msg) *err_msg = g_strdup("Read timeout or connection closed while reading frame header");
        return false;
    }

    uint32_t payload_len = uv_read_u32_le(len_bytes);
    if (payload_len > UV_MAX_FRAME_SIZE) {
        if (err_msg) *err_msg = g_strdup_printf("Frame length %u exceeds maximum %d", payload_len, UV_MAX_FRAME_SIZE);
        return false;
    }

    uint8_t *payload = NULL;
    if (payload_len > 0) {
        payload = g_malloc(payload_len);
        if (!read_exact(sock, payload, payload_len, timeout_ms)) {
            g_free(payload);
            if (err_msg) *err_msg = g_strdup("Truncated frame payload");
            return false;
        }
    }

    *out_payload = payload;
    *out_len = payload_len;
    return true;
}

bool uv_tls_write_frame(UvTlsSocket *sock, const uint8_t *payload, size_t len)
{
    if (!sock || !sock->ssl) return false;

    uint8_t *frame = NULL;
    size_t frame_len = 0;
    if (!uv_frame_encode(payload, len, &frame, &frame_len)) {
        return false;
    }

    size_t written = 0;
    while (written < frame_len) {
        int r = SSL_write(sock->ssl, frame + written, (int)(frame_len - written));
        if (r <= 0) {
            g_free(frame);
            return false;
        }
        written += (size_t)r;
    }

    g_free(frame);
    return true;
}
