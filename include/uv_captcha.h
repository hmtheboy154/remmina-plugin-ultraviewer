/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Captcha Challenge Handling
 *
 * Mirrors LinuxViewer's recovered flow: when the command server answers a
 * registration with "60:<esc>:<extra>" instead of the "3:" reply, the client
 * must open enter_captcha.aspx in a browser. The page reports the solved
 * token through window.external.FromJS_CaptchaCorrect(_, token), and the next
 * registration carries that token in its last field.
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef UV_CAPTCHA_H
#define UV_CAPTCHA_H

#include "uv_types.h"

typedef struct {
    char esc[1024];    /* Opaque server session, sent back as esc= */
    char extra[1024];  /* Optional opaque value, sent back as extra= */
} UvCaptchaChallenge;

/* Parses a "60:<esc>:<extra>" system payload. Returns false when the payload
 * is not a well-formed captcha challenge. */
bool uv_captcha_parse_challenge(const char *reply, UvCaptchaChallenge *out);

/* Builds the enter_captcha.aspx page URL for a specific language ("vi" or "en")
 * or for the current locale when lang is NULL. Caller frees with g_free(). */
char *uv_captcha_build_url_for_lang(const UvCaptchaChallenge *challenge, const char *lang);
char *uv_captcha_build_url(const UvCaptchaChallenge *challenge);

/* Token must be printable ASCII without ':' so it fits one registration
 * field, and no longer than UV_CAPTCHA_TOKEN_MAX. */
bool uv_captcha_token_valid(const char *token);

/* Process-wide in-memory cache of the most recently solved captcha token,
 * matching the Windows desktop client (global 0x7e21dc) and LinuxViewer. */
void uv_captcha_get_cached_token(char *out_buf, size_t out_len);
void uv_captcha_set_cached_token(const char *token);

/* Shows the captcha page in a modal GTK window and blocks the calling
 * (worker) thread until it is solved or dismissed. All UI work runs on the
 * GTK main loop; parent_widget is a GtkWidget used to find the transient
 * parent window (may be NULL). Returns a g_free()-able token, or NULL when
 * dismissed or when this build has no WebKit support. */
char *uv_captcha_request_token(const UvCaptchaChallenge *challenge, gpointer parent_widget);

/* Closes the captcha window(s) opened for the connection identified by
 * owner (the parent_widget passed to uv_captcha_request_token()), so a
 * blocking uv_captcha_request_token() returns NULL. Other connections'
 * windows are left alone. */
void uv_captcha_cancel_dialog(gpointer owner);

#endif /* UV_CAPTCHA_H */
