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

#include "uv_crypto.h"
#include <openssl/evp.h>
#include <openssl/rand.h>

void uv_md5_bytes(const uint8_t *data, size_t len, uint8_t out[16])
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return;
    EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    unsigned int md_len = 16;
    EVP_DigestFinal_ex(ctx, out, &md_len);
    EVP_MD_CTX_free(ctx);
}

void uv_md5_hex(const char *input, char output[33])
{
    uint8_t digest[16];
    uv_md5_bytes((const uint8_t *)input, strlen(input), digest);
    for (int i = 0; i < 16; i++) {
        sprintf(output + (i * 2), "%02x", digest[i]);
    }
    output[32] = '\0';
}

void uv_sha1(const uint8_t *data, size_t len, uint8_t out[20])
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return;
    EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    unsigned int md_len = 20;
    EVP_DigestFinal_ex(ctx, out, &md_len);
    EVP_MD_CTX_free(ctx);
}

bool uv_utf8_to_utf16le(const char *str, uint8_t **out_bytes, size_t *out_len)
{
    if (!str || !out_bytes || !out_len) return false;

    glong items_written = 0;
    GError *error = NULL;
    gunichar2 *u16 = g_utf8_to_utf16(str, -1, NULL, &items_written, &error);
    if (!u16 || error) {
        if (error) g_error_free(error);
        return false;
    }

    size_t byte_count = (size_t)items_written * sizeof(gunichar2);
    uint8_t *buf = g_malloc(byte_count);
    for (glong i = 0; i < items_written; i++) {
        uv_write_u16_le(buf + (i * 2), (uint16_t)u16[i]);
    }
    g_free(u16);

    *out_bytes = buf;
    *out_len = byte_count;
    return true;
}

bool uv_utf16le_to_utf8(const uint8_t *bytes, size_t len, char **out_str)
{
    if (!bytes || len % 2 != 0 || !out_str) return false;

    glong n_units = (glong)(len / 2);
    gunichar2 *u16 = g_new(gunichar2, n_units + 1);
    for (glong i = 0; i < n_units; i++) {
        u16[i] = (gunichar2)uv_read_u16_le(bytes + (i * 2));
    }
    u16[n_units] = 0;

    GError *error = NULL;
    glong items_written = 0;
    char *utf8 = g_utf16_to_utf8(u16, n_units, NULL, &items_written, &error);
    g_free(u16);

    if (!utf8 || error) {
        if (error) g_error_free(error);
        return false;
    }

    *out_str = utf8;
    return true;
}

static void derive_3des_key_iv(const uint8_t *utf16_pwd, size_t pwd_len, uint8_t key[24], uint8_t iv[8])
{
    uint8_t sha_key[20];
    uv_sha1(utf16_pwd, pwd_len, sha_key);
    memcpy(key, sha_key, 20);
    memset(key + 20, 0, 4);

    uint8_t sha_iv[20];
    uv_sha1((const uint8_t *)"", 0, sha_iv);
    memcpy(iv, sha_iv, 8);
}

bool uv_3des_encrypt(const uint8_t key[24], const uint8_t iv[8],
                     const uint8_t *plain, size_t plain_len,
                     uint8_t **out_cipher, size_t *out_cipher_len)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    if (EVP_EncryptInit_ex(ctx, EVP_des_ede3_cbc(), NULL, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    /* PKCS#7 padding */
    EVP_CIPHER_CTX_set_padding(ctx, 1);

    size_t max_out = plain_len + 8;
    uint8_t *cipher = g_malloc(max_out);
    int out1 = 0, out2 = 0;

    if (EVP_EncryptUpdate(ctx, cipher, &out1, plain, (int)plain_len) != 1 ||
        EVP_EncryptFinal_ex(ctx, cipher + out1, &out2) != 1) {
        g_free(cipher);
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    EVP_CIPHER_CTX_free(ctx);
    *out_cipher = cipher;
    *out_cipher_len = (size_t)(out1 + out2);
    return true;
}

bool uv_3des_decrypt(const uint8_t key[24], const uint8_t iv[8],
                     const uint8_t *cipher, size_t cipher_len,
                     uint8_t **out_plain, size_t *out_plain_len)
{
    if (cipher_len == 0 || cipher_len % 8 != 0) return false;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    if (EVP_DecryptInit_ex(ctx, EVP_des_ede3_cbc(), NULL, key, iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    EVP_CIPHER_CTX_set_padding(ctx, 1);

    uint8_t *plain = g_malloc(cipher_len);
    int out1 = 0, out2 = 0;

    if (EVP_DecryptUpdate(ctx, plain, &out1, cipher, (int)cipher_len) != 1 ||
        EVP_DecryptFinal_ex(ctx, plain + out1, &out2) != 1) {
        g_free(plain);
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    EVP_CIPHER_CTX_free(ctx);
    *out_plain = plain;
    *out_plain_len = (size_t)(out1 + out2);
    return true;
}

/* Default server password: \u0005 \u000A \u000F \u0014 \u0019 */
static const uint8_t UV_SERVER_PWD_UTF16LE[] = {
    0x05, 0x00, 0x0A, 0x00, 0x0F, 0x00, 0x14, 0x00, 0x19, 0x00
};

bool uv_server_cipher_encrypt(const uint8_t *plain, size_t plain_len,
                              uint8_t **out_cipher, size_t *out_cipher_len)
{
    uint8_t key[24], iv[8];
    derive_3des_key_iv(UV_SERVER_PWD_UTF16LE, sizeof(UV_SERVER_PWD_UTF16LE), key, iv);
    return uv_3des_encrypt(key, iv, plain, plain_len, out_cipher, out_cipher_len);
}

bool uv_server_cipher_decrypt(const uint8_t *cipher, size_t cipher_len,
                              uint8_t **out_plain, size_t *out_plain_len)
{
    uint8_t key[24], iv[8];
    derive_3des_key_iv(UV_SERVER_PWD_UTF16LE, sizeof(UV_SERVER_PWD_UTF16LE), key, iv);
    return uv_3des_decrypt(key, iv, cipher, cipher_len, out_plain, out_plain_len);
}

bool uv_custom_cipher_encrypt(const char *password,
                              const uint8_t *plain, size_t plain_len,
                              uint8_t **out_cipher, size_t *out_cipher_len)
{
    uint8_t *utf16 = NULL;
    size_t utf16_len = 0;
    if (!uv_utf8_to_utf16le(password, &utf16, &utf16_len)) return false;

    uint8_t key[24], iv[8];
    derive_3des_key_iv(utf16, utf16_len, key, iv);
    g_free(utf16);

    return uv_3des_encrypt(key, iv, plain, plain_len, out_cipher, out_cipher_len);
}

bool uv_custom_cipher_decrypt(const char *password,
                              const uint8_t *cipher, size_t cipher_len,
                              uint8_t **out_plain, size_t *out_plain_len)
{
    uint8_t *utf16 = NULL;
    size_t utf16_len = 0;
    if (!uv_utf8_to_utf16le(password, &utf16, &utf16_len)) return false;

    uint8_t key[24], iv[8];
    derive_3des_key_iv(utf16, utf16_len, key, iv);
    g_free(utf16);

    return uv_3des_decrypt(key, iv, cipher, cipher_len, out_plain, out_plain_len);
}

void uv_random_bytes(uint8_t *buf, size_t len)
{
    if (RAND_bytes(buf, (int)len) != 1) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = (uint8_t)(g_random_int() & 0xFF);
        }
    }
}

void uv_random_string(char *out, size_t len, const char *alphabet)
{
    size_t alpha_len = strlen(alphabet);
    uint8_t *r = g_malloc(len * 2);
    uv_random_bytes(r, len * 2);

    size_t filled = 0;
    size_t r_idx = 0;
    size_t limit = 256 - (256 % alpha_len);

    while (filled < len) {
        if (r_idx >= len * 2) {
            uv_random_bytes(r, len * 2);
            r_idx = 0;
        }
        uint8_t b = r[r_idx++];
        if (b < limit) {
            out[filled++] = alphabet[b % alpha_len];
        }
    }
    out[len] = '\0';
    g_free(r);
}

void uv_random_hex(char *out, size_t hex_len, bool uppercase)
{
    const char *charset = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    uv_random_string(out, hex_len, charset);
}
