/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Command Channel
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "uv_command.h"
#include "uv_crypto.h"
#include "uv_wire.h"
#include "uv_captcha.h"
#include <sys/utsname.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>

static char *get_primary_local_ip(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &sin.sin_addr);
        if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
            struct sockaddr_in local_sin;
            socklen_t len = sizeof(local_sin);
            if (getsockname(fd, (struct sockaddr *)&local_sin, &len) == 0) {
                char ip_buf[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &local_sin.sin_addr, ip_buf, sizeof(ip_buf))) {
                    close(fd);
                    if (!g_str_has_prefix(ip_buf, "127.")) {
                        return g_strdup(ip_buf);
                    }
                }
            }
        }
        close(fd);
    }
    return g_strdup("192.168.1.100");
}

bool uv_identity_init(UvClientIdentity *ident)
{
    if (!ident) return false;
    memset(ident, 0, sizeof(*ident));

    /* Load or persist stable device_id (32 uppercase hex) and reg_password (8 digits)
     * in ~/.config/remmina/ultraviewer.identity so we don't register a brand-new ID
     * on the UltraViewer server on every single connection attempt. */
    bool loaded = false;
    const char *cfg_dir = g_get_user_config_dir();
    char *ident_path = NULL;
    if (cfg_dir && *cfg_dir) {
        char *remmina_dir = g_build_filename(cfg_dir, "remmina", NULL);
        g_mkdir_with_parents(remmina_dir, 0700);
        ident_path = g_build_filename(remmina_dir, "ultraviewer.identity", NULL);
        g_free(remmina_dir);

        char *contents = NULL;
        gsize len = 0;
        if (g_file_get_contents(ident_path, &contents, &len, NULL) && contents) {
            char **lines = g_strsplit_set(contents, "\r\n", -1);
            if (lines && lines[0] && lines[1] &&
                strlen(lines[0]) == 32 && strlen(lines[1]) == 8) {
                bool valid = true;
                for (int i = 0; i < 32; i++) {
                    char c = lines[0][i];
                    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) valid = false;
                }
                for (int i = 0; i < 8; i++) {
                    char c = lines[1][i];
                    if (!(c >= '0' && c <= '9')) valid = false;
                }
                if (valid) {
                    g_strlcpy(ident->device_id, lines[0], sizeof(ident->device_id));
                    g_strlcpy(ident->reg_password, lines[1], sizeof(ident->reg_password));
                    loaded = true;
                }
            }
            g_strfreev(lines);
            g_free(contents);
        }
    }

    if (!loaded) {
        uv_random_hex(ident->device_id, 32, true);
        uv_random_string(ident->reg_password, 8, "0123456789");
        if (ident_path) {
            char *data = g_strdup_printf("%s\n%s\n", ident->device_id, ident->reg_password);
            g_file_set_contents(ident_path, data, -1, NULL);
            g_free(data);
        }
    }
    g_free(ident_path);

    /* Locally administered unicast MAC derived from device ID (02:xx:xx:xx:xx:xx) */
    snprintf(ident->mac_address, sizeof(ident->mac_address), "02%.10s", ident->device_id);
    return true;
}

static char *encode_wire_username(const char *username)
{
    if (!username || !*username) {
        return g_strdup("user");
    }
    bool clean_ascii = true;
    for (const unsigned char *p = (const unsigned char *)username; *p; p++) {
        if (*p < 0x20 || *p > 0x7e || *p == ':') {
            clean_ascii = false;
            break;
        }
    }
    if (clean_ascii) {
        return g_strdup(username);
    }
    return g_base64_encode((const guchar *)username, strlen(username));
}

static char *get_b64_hostname(void)
{
    const char *host = g_get_host_name();
    if (!host || !*host) {
        host = "linux-client";
    }
    return g_base64_encode((const guchar *)host, strlen(host));
}

bool uv_parse_registration_reply(const char *reply_str, UvClientIdentity *ident, char **err_msg)
{
    if (err_msg) *err_msg = NULL;
    if (!reply_str || !ident) {
        if (err_msg) *err_msg = g_strdup("Invalid parameters for registration reply parser");
        return false;
    }

    char **tokens = uv_split_fields(reply_str, ':', 25);
    if (!tokens || g_strv_length(tokens) < 11 || g_strcmp0(tokens[0], "3") != 0) {
        if (err_msg) {
            *err_msg = g_strdup_printf("Malformed registration reply (tokens=%u, raw='%s')",
                                       tokens ? g_strv_length(tokens) : 0, reply_str);
        }
        g_strfreev(tokens);
        return false;
    }

    ident->public_port = (uint16_t)atoi(tokens[1]);
    /* Remove leading zeros from assigned ID */
    const char *id_tok = tokens[2];
    while (*id_tok == '0' && *(id_tok + 1) != '\0') id_tok++;
    g_strlcpy(ident->assigned_id, id_tok, sizeof(ident->assigned_id));
    g_strlcpy(ident->public_ip, tokens[3], sizeof(ident->public_ip));
    g_strlcpy(ident->server_token, tokens[10], sizeof(ident->server_token));

    g_strfreev(tokens);

    if (strlen(ident->assigned_id) == 0 || ident->public_port == 0) {
        if (err_msg) *err_msg = g_strdup("Server assigned invalid ID or port");
        return false;
    }

    return true;
}

bool uv_command_register(UvTlsSocket *sock, UvClientIdentity *ident,
                         const char *captcha_token,
                         UvCaptchaChallenge *out_captcha, char **err_msg)
{
    if (!sock || !ident) return false;
    if (err_msg) *err_msg = NULL;
    if (out_captcha) memset(out_captcha, 0, sizeof(*out_captcha));

    char *b64_host = get_b64_hostname();
    char *local_ip = get_primary_local_ip();
    char *wire_user = encode_wire_username(g_get_user_name());

    /* UltraViewer server parses OS version as .NET System.Version:
     * Build|Major|MajorRevision|Minor|MinorRevision|Revision
     * All components MUST be integers, matching MacViewer: c|a|0|b|0|0 */
    struct utsname un;
    char os_ver[64];
    unsigned int v_maj = 6, v_min = 0, v_build = 0;
    if (uname(&un) == 0) {
        sscanf(un.release, "%u.%u.%u", &v_maj, &v_min, &v_build);
    }
    snprintf(os_ver, sizeof(os_ver), "%u|%u|0|%u|0|0", v_build, v_maj, v_min);

    const char *fields[28];
    fields[0]  = "3";
    fields[1]  = ident->device_id;
    fields[2]  = ident->reg_password;
    fields[3]  = "0";
    fields[4]  = local_ip;
    fields[5]  = "0";
    fields[6]  = b64_host;
    fields[7]  = UV_CLIENT_VERSION;
    fields[8]  = ident->mac_address;
    fields[9]  = "";
    fields[10] = os_ver;
    fields[11] = "0";
    fields[12] = "";
    fields[13] = "";
    fields[14] = "0";
    fields[15] = "";
    fields[16] = wire_user;
    fields[17] = "0";
    fields[18] = "0";
    fields[19] = "0";
    fields[20] = "0";
    fields[21] = "0";
    fields[22] = "0";
    fields[23] = "0";
    fields[24] = "0";
    fields[25] = "";
    fields[26] = "";
    /* Desktop global 0x7e21dc: the token from UltraViewer's captcha page,
     * empty until one is solved. */
    fields[27] = captcha_token ? captcha_token : "";

    char *cmd_str = uv_join_fields(fields, 28);
    g_free(b64_host);
    g_free(local_ip);
    g_free(wire_user);

    uint8_t *ciphertext = NULL;
    size_t cipher_len = 0;
    if (!uv_server_cipher_encrypt((const uint8_t *)cmd_str, strlen(cmd_str),
                                  &ciphertext, &cipher_len)) {
        g_free(cmd_str);
        if (err_msg) *err_msg = g_strdup("Failed to encrypt registration command");
        return false;
    }
    g_free(cmd_str);

    uint8_t *env = NULL;
    size_t env_len = 0;
    uv_system_envelope_encode(ciphertext, cipher_len, true, &env, &env_len);
    g_free(ciphertext);

    if (!uv_tls_write_frame(sock, env, env_len)) {
        g_free(env);
        if (err_msg) *err_msg = g_strdup("Failed to send registration frame");
        return false;
    }
    g_free(env);

    /* Loop until a "3:" registration reply arrives or the 20s deadline expires.
     * When the service wants a person to solve a captcha first, it answers
     * with "60:<esc>:<extra>" instead of the "3:" reply; report that
     * immediately via out_captcha. */
    gint64 deadline_us = g_get_monotonic_time() + (20 * G_USEC_PER_SEC);
    bool seen_challenge = false;
    while (true) {
        gint64 remaining_us = deadline_us - g_get_monotonic_time();
        if (remaining_us <= 0) {
            if (err_msg) {
                *err_msg = g_strdup(seen_challenge
                    ? "UltraViewer server sent a challenge (frame 60) instead of registering this client, "
                      "most likely a CAPTCHA/rate-limit for this IP. Try again later or from another network."
                    : "Registration timed out waiting for server reply");
            }
            return false;
        }
        int timeout_ms = (int)(remaining_us / 1000);
        if (timeout_ms < 100) timeout_ms = 100;

        uint8_t *reply = NULL;
        size_t reply_len = 0;
        char *read_err = NULL;
        if (!uv_tls_read_frame(sock, &reply, &reply_len, timeout_ms, &read_err)) {
            if (seen_challenge) {
                g_free(read_err);
                if (err_msg) {
                    *err_msg = g_strdup("UltraViewer server sent a challenge (frame 60) instead of registering this client, "
                                        "most likely a CAPTCHA/rate-limit for this IP. Try again later or from another network.");
                }
            } else if (err_msg) {
                *err_msg = read_err;
            } else {
                g_free(read_err);
            }
            return false;
        }

        /* Skip empty keepalive frames or non-system envelopes */
        if (reply_len < 2 || reply[0] != 0x01) {
            g_free(reply);
            continue;
        }

        char *reply_str = g_strndup((char *)reply + 1, reply_len - 1);
        g_free(reply);

        /* A captcha challenge replaces the registration reply: report it and
         * let the caller solve it before registering again. */
        if (g_str_has_prefix(reply_str, "60:")) {
            if (out_captcha && uv_captcha_parse_challenge(reply_str, out_captcha)) {
                g_free(reply_str);
                if (err_msg) {
                    *err_msg = g_strdup("UltraViewer requires solving a captcha before this computer can connect");
                }
                return false;
            }
            seen_challenge = true;
        }

        /* Skip unsolicited server commands (such as "60:...") until "3:" arrives */
        if (!g_str_has_prefix(reply_str, "3:")) {
            g_free(reply_str);
            continue;
        }

        bool ok = uv_parse_registration_reply(reply_str, ident, err_msg);
        g_free(reply_str);
        return ok;
    }
}

bool uv_command_lookup(UvTlsSocket *sock, const UvClientIdentity *ident,
                       const char *target_id, const char *target_password,
                       UvPartnerLookup *out_lookup, char **err_msg)
{
    if (!sock || !ident || !target_id || !target_password || !out_lookup) return false;
    memset(out_lookup, 0, sizeof(*out_lookup));

    /* Proof 1: MD5(" " * 32 + partner_password) */
    char padded_pw[128];
    snprintf(padded_pw, sizeof(padded_pw), "                                %s", target_password);
    char target_pw_md5[33];
    uv_md5_hex(padded_pw, target_pw_md5);

    /* Proof 2: MD5(assigned_id + registration_password) */
    char local_creds[128];
    snprintf(local_creds, sizeof(local_creds), "%s%s", ident->assigned_id, ident->reg_password);
    char local_pw_md5[33];
    uv_md5_hex(local_creds, local_pw_md5);

    int corr_id = g_random_int_range(100000, 999999999);
    char corr_str[32];
    snprintf(corr_str, sizeof(corr_str), "%d", corr_id);

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    int tz_minutes = (int)(lt->tm_gmtoff / 60);
    char tz_str[32];
    snprintf(tz_str, sizeof(tz_str), "%d", tz_minutes);

    const char *fields[17];
    fields[0]  = "8";
    fields[1]  = target_id;
    fields[2]  = target_pw_md5;
    fields[3]  = UV_CLIENT_VERSION;
    fields[4]  = "";
    fields[5]  = "";
    fields[6]  = ident->assigned_id;
    fields[7]  = local_pw_md5;
    fields[8]  = "-1";
    fields[9]  = corr_str;
    fields[10] = "0";
    fields[11] = "0";
    fields[12] = "0";
    fields[13] = tz_str;
    fields[14] = "1";
    fields[15] = "";
    fields[16] = "";

    char *cmd_str = uv_join_fields(fields, 17);

    uint8_t *ciphertext = NULL;
    size_t cipher_len = 0;
    if (!uv_server_cipher_encrypt((const uint8_t *)cmd_str, strlen(cmd_str),
                                  &ciphertext, &cipher_len)) {
        g_free(cmd_str);
        if (err_msg) *err_msg = g_strdup("Failed to encrypt lookup command");
        return false;
    }
    g_free(cmd_str);

    uint8_t *env = NULL;
    size_t env_len = 0;
    uv_system_envelope_encode(ciphertext, cipher_len, true, &env, &env_len);
    g_free(ciphertext);

    if (!uv_tls_write_frame(sock, env, env_len)) {
        g_free(env);
        if (err_msg) *err_msg = g_strdup("Failed to write lookup frame");
        return false;
    }
    g_free(env);

    /* Loop until a matching "8:" lookup reply arrives or the 20s deadline expires. */
    gint64 deadline_us = g_get_monotonic_time() + (20 * G_USEC_PER_SEC);
    while (true) {
        gint64 remaining_us = deadline_us - g_get_monotonic_time();
        if (remaining_us <= 0) {
            if (err_msg) *err_msg = g_strdup("Partner lookup timed out waiting for server reply");
            return false;
        }
        int timeout_ms = (int)(remaining_us / 1000);
        if (timeout_ms < 100) timeout_ms = 100;

        uint8_t *reply = NULL;
        size_t reply_len = 0;
        if (!uv_tls_read_frame(sock, &reply, &reply_len, timeout_ms, err_msg)) {
            return false;
        }

        if (reply_len < 2 || reply[0] != 0x01) {
            g_free(reply);
            continue;
        }

        char *reply_str = g_strndup((char *)reply + 1, reply_len - 1);
        g_free(reply);

        if (!g_str_has_prefix(reply_str, "8:")) {
            g_free(reply_str);
            continue;
        }

        char *parse_err = NULL;
        bool res = uv_parse_lookup_reply(reply_str, corr_str, out_lookup, &parse_err);
        g_free(reply_str);

        if (!res && parse_err && strstr(parse_err, "Correlation mismatch") != NULL) {
            g_free(parse_err);
            continue;
        }

        if (err_msg) {
            *err_msg = parse_err;
        } else {
            g_free(parse_err);
        }
        return res;
    }
}

bool uv_parse_lookup_reply(const char *reply_str, const char *expected_corr,
                           UvPartnerLookup *out_lookup, char **err_msg)
{
    if (err_msg) *err_msg = NULL;
    if (!reply_str || !out_lookup) {
        if (err_msg) *err_msg = g_strdup("Invalid parameters for lookup response parser");
        return false;
    }

    memset(out_lookup, 0, sizeof(*out_lookup));

    char **tokens = uv_split_fields(reply_str, ':', 0);
    if (!tokens) {
        if (err_msg) *err_msg = g_strdup("Malformed lookup reply from server");
        return false;
    }

    guint n_tokens = g_strv_length(tokens);
    if (n_tokens < 2 || g_strcmp0(tokens[0], "8") != 0) {
        g_strfreev(tokens);
        if (err_msg) *err_msg = g_strdup("Malformed lookup reply from server");
        return false;
    }

    /* Verify correlation ID (last token) if expected_corr is specified */
    if (expected_corr && *expected_corr) {
        if (n_tokens >= 3 && g_strcmp0(tokens[n_tokens - 1], expected_corr) != 0) {
            g_strfreev(tokens);
            if (err_msg) *err_msg = g_strdup("Correlation mismatch in lookup response");
            return false;
        }
    }

    out_lookup->status = atoi(tokens[1]);
    if (out_lookup->status == 2) {
        g_strfreev(tokens);
        if (err_msg) *err_msg = g_strdup("UltraViewer rejected partner password. Please check the password and try again.");
        return false;
    } else if (out_lookup->status == 0 || out_lookup->status == -1) {
        g_strfreev(tokens);
        if (err_msg) *err_msg = g_strdup("The partner ID is offline.");
        return false;
    } else if (out_lookup->status != 1) {
        int code = out_lookup->status;
        g_strfreev(tokens);
        if (err_msg) *err_msg = g_strdup_printf("UltraViewer declined partner lookup (code %d)", code);
        return false;
    }

    if (n_tokens < 15) {
        g_strfreev(tokens);
        if (err_msg) *err_msg = g_strdup("Incomplete lookup routing details from server");
        return false;
    }

    g_strlcpy(out_lookup->partner_ip, tokens[4], sizeof(out_lookup->partner_ip));
    out_lookup->target_port = (uint16_t)atoi(tokens[9]);
    g_strlcpy(out_lookup->relay_ip, tokens[11], sizeof(out_lookup->relay_ip));
    g_strlcpy(out_lookup->relay_hostname, tokens[14], sizeof(out_lookup->relay_hostname));
    if (n_tokens > 20) {
        g_strlcpy(out_lookup->secret_token, tokens[20], sizeof(out_lookup->secret_token));
    }

    if (tokens[8] && *tokens[8]) {
        gsize dlen = 0;
        guchar *dec = g_base64_decode(tokens[8], &dlen);
        if (dec) {
            size_t copylen = MIN(dlen, sizeof(out_lookup->partner_name) - 1);
            memcpy(out_lookup->partner_name, dec, copylen);
            out_lookup->partner_name[copylen] = '\0';
            g_free(dec);
        }
    }

    g_strfreev(tokens);
    return true;
}

static void sanitize_display_name(const char *in, char *out, size_t max_len)
{
    if (!in || !*in) {
        const char *h = g_get_host_name();
        if (h && *h) {
            snprintf(out, max_len, "Remmina - %s", h);
        } else {
            g_strlcpy(out, "Remmina", max_len);
        }
        return;
    }
    size_t pos = 0;
    for (size_t i = 0; in[i] != '\0' && pos < max_len - 1; i++) {
        char c = in[i];
        if (strchr("!#$&?:/\\*,.@<>;'\"=", c) == NULL && (c >= 32 && c <= 126)) {
            out[pos++] = c;
        }
    }
    out[pos] = '\0';
    if (pos == 0) {
        const char *h = g_get_host_name();
        if (h && *h) {
            snprintf(out, max_len, "Remmina - %s", h);
        } else {
            g_strlcpy(out, "Remmina", max_len);
        }
    }
}

char *uv_create_login_message(const UvClientIdentity *ident,
                              const UvPartnerLookup *lookup,
                              const char *partner_password,
                              const char *display_name,
                              int control_mode,
                              int quality,
                              const char *connection_hash)
{
    char nonce[9];
    uv_random_string(nonce, 8, "0123456789");

    char nonce_pw[128];
    snprintf(nonce_pw, sizeof(nonce_pw), "%s%s", nonce, partner_password);
    char pw_proof[33];
    uv_md5_hex(nonce_pw, pw_proof);

    char padded_pw[128];
    snprintf(padded_pw, sizeof(padded_pw), "                                %s", partner_password);
    char pw_proof32[33];
    uv_md5_hex(padded_pw, pw_proof32);

    char clean_name[64];
    sanitize_display_name(display_name, clean_name, sizeof(clean_name));

    char control_str[16];
    snprintf(control_str, sizeof(control_str), "%d", control_mode);

    char quality_str[16];
    snprintf(quality_str, sizeof(quality_str), "%d", quality > 0 ? quality : 45);

    const char *fields[23];
    fields[0]  = "login";
    fields[1]  = nonce;
    fields[2]  = pw_proof;
    fields[3]  = "1920";
    fields[4]  = "1080";
    fields[5]  = "0";
    fields[6]  = "15";
    fields[7]  = "15";
    fields[8]  = "0";
    fields[9]  = "0";
    fields[10] = ident->assigned_id;
    fields[11] = clean_name;
    fields[12] = control_str;
    fields[13] = pw_proof32;
    fields[14] = "";
    fields[15] = connection_hash;
    fields[16] = "2"; /* RGB555 mode */
    fields[17] = quality_str;
    fields[18] = "0";
    fields[19] = "0";
    fields[20] = lookup->secret_token;
    fields[21] = UV_CLIENT_VERSION;
    fields[22] = "0";

    return uv_join_fields(fields, 23);
}
