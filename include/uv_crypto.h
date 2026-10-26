/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Cryptographic Helpers
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UV_CRYPTO_H
#define UV_CRYPTO_H

#include "uv_types.h"

void uv_md5_hex(const char *input, char output[33]);
void uv_md5_bytes(const uint8_t *data, size_t len, uint8_t out[16]);
void uv_sha1(const uint8_t *data, size_t len, uint8_t out[20]);

bool uv_utf8_to_utf16le(const char *str, uint8_t **out_bytes, size_t *out_len);
bool uv_utf16le_to_utf8(const uint8_t *bytes, size_t len, char **out_str);

bool uv_3des_encrypt(const uint8_t key[24], const uint8_t iv[8],
                     const uint8_t *plain, size_t plain_len,
                     uint8_t **out_cipher, size_t *out_cipher_len);

bool uv_3des_decrypt(const uint8_t key[24], const uint8_t iv[8],
                     const uint8_t *cipher, size_t cipher_len,
                     uint8_t **out_plain, size_t *out_plain_len);

bool uv_server_cipher_encrypt(const uint8_t *plain, size_t plain_len,
                              uint8_t **out_cipher, size_t *out_cipher_len);

bool uv_server_cipher_decrypt(const uint8_t *cipher, size_t cipher_len,
                              uint8_t **out_plain, size_t *out_plain_len);

bool uv_custom_cipher_encrypt(const char *password,
                              const uint8_t *plain, size_t plain_len,
                              uint8_t **out_cipher, size_t *out_cipher_len);

bool uv_custom_cipher_decrypt(const char *password,
                              const uint8_t *cipher, size_t cipher_len,
                              uint8_t **out_plain, size_t *out_plain_len);

void uv_random_bytes(uint8_t *buf, size_t len);
void uv_random_string(char *out, size_t len, const char *alphabet);
void uv_random_hex(char *out, size_t hex_len, bool uppercase);

#endif /* UV_CRYPTO_H */
