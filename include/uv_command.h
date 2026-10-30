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

#ifndef UV_COMMAND_H
#define UV_COMMAND_H

#include "uv_types.h"
#include "uv_tls.h"

typedef struct {
    char device_id[33];
    char reg_password[9];
    char mac_address[13];
    char assigned_id[32];
    char public_ip[16];
    uint16_t public_port;
    char server_token[128];
} UvClientIdentity;

typedef struct {
    int status;
    char partner_ip[16];
    uint16_t target_port;
    char relay_ip[16];
    char relay_hostname[128];
    char secret_token[128];
    char partner_name[64];
} UvPartnerLookup;

bool uv_identity_init(UvClientIdentity *ident);

bool uv_command_register(UvTlsSocket *sock, UvClientIdentity *ident, char **err_msg);

bool uv_parse_registration_reply(const char *reply_str, UvClientIdentity *ident, char **err_msg);

bool uv_command_lookup(UvTlsSocket *sock, const UvClientIdentity *ident,
                       const char *target_id, const char *target_password,
                       UvPartnerLookup *out_lookup, char **err_msg);

bool uv_parse_lookup_reply(const char *reply_str, const char *expected_corr,
                           UvPartnerLookup *out_lookup, char **err_msg);

char *uv_create_login_message(const UvClientIdentity *ident,
                              const UvPartnerLookup *lookup,
                              const char *partner_password,
                              const char *display_name,
                              int control_mode,
                              int quality,
                              const char *connection_hash);

#endif /* UV_COMMAND_H */
