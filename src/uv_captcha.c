/*
 * Remmina - The GTK+ Remote Desktop Client
 * UltraViewer Protocol Plugin - Captcha Challenge Handling
 *
 * Copyright (C) 2026 hmtheboy154 <buingoc67@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "uv_captcha.h"
#include "uv_wire.h"

#ifndef _
#define _(String) (String)
#endif

#include <string.h>

/* ------------------------------------------------------------------ *
 * Shared helpers (always compiled)                                   *
 * ------------------------------------------------------------------ */

static GMutex cached_token_lock;
static char cached_captcha_token[UV_CAPTCHA_TOKEN_MAX + 1] = { 0 };

void uv_captcha_get_cached_token(char *out_buf, size_t out_len)
{
    if (!out_buf || out_len == 0) return;
    g_mutex_lock(&cached_token_lock);
    g_strlcpy(out_buf, cached_captcha_token, out_len);
    g_mutex_unlock(&cached_token_lock);
}

void uv_captcha_set_cached_token(const char *token)
{
    g_mutex_lock(&cached_token_lock);
    if (token && uv_captcha_token_valid(token)) {
        g_strlcpy(cached_captcha_token, token, sizeof(cached_captcha_token));
    } else {
        cached_captcha_token[0] = '\0';
    }
    g_mutex_unlock(&cached_token_lock);
}

static bool is_colon_field(const char *value)
{
    if (!value) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p < 0x20 || *p > 0x7e || *p == ':') {
            return false;
        }
    }
    return true;
}

static char *uv_percent_encode(const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    GString *out = g_string_sized_new(strlen(value));
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        unsigned char c = *p;
        if (g_ascii_isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            g_string_append_c(out, (gchar)c);
        } else {
            g_string_append_c(out, '%');
            g_string_append_c(out, hex[c >> 4]);
            g_string_append_c(out, hex[c & 0x0F]);
        }
    }
    return g_string_free(out, FALSE);
}

/* The desktop client sends its UI language to the captcha page; UltraViewer
 * ships Vietnamese and English pages. */
static const char *captcha_page_language(void)
{
    const char *lang = g_getenv("LANGUAGE");
    if (!lang || !*lang) lang = g_getenv("LC_ALL");
    if (!lang || !*lang) lang = g_getenv("LC_MESSAGES");
    if (!lang || !*lang) lang = g_getenv("LANG");
    return (lang && g_str_has_prefix(lang, "vi")) ? "vi" : "en";
}

bool uv_captcha_parse_challenge(const char *reply, UvCaptchaChallenge *out)
{
    if (!reply || !out) return false;
    memset(out, 0, sizeof(*out));

    if (strlen(reply) > 4096) return false;

    char **fields = uv_split_fields(reply, ':', 0);
    if (!fields) return false;

    guint n_fields = g_strv_length(fields);
    bool ok = false;
    if (n_fields >= 2 && g_strcmp0(fields[0], "60") == 0 &&
        fields[1][0] != '\0' && strlen(fields[1]) < sizeof(out->esc) &&
        is_colon_field(fields[1])) {
        const char *extra = (n_fields >= 3) ? fields[2] : "";
        if (strlen(extra) < sizeof(out->extra) && is_colon_field(extra)) {
            g_strlcpy(out->esc, fields[1], sizeof(out->esc));
            g_strlcpy(out->extra, extra, sizeof(out->extra));
            ok = true;
        }
    }

    g_strfreev(fields);
    return ok;
}

char *uv_captcha_build_url_for_lang(const UvCaptchaChallenge *challenge, const char *lang)
{
    if (!challenge) return NULL;
    const char *effective_lang = (lang && *lang) ? lang : captcha_page_language();
    char *enc_lang = uv_percent_encode(effective_lang);
    char *enc_ver = uv_percent_encode(UV_CAPTCHA_VERSION);
    char *esc = uv_percent_encode(challenge->esc);
    char *extra = uv_percent_encode(challenge->extra);
    char *url = g_strdup_printf(UV_CAPTCHA_PAGE "?lang=%s&version=%s&extra=%s&esc=%s",
                                enc_lang, enc_ver, extra, esc);
    g_free(enc_lang);
    g_free(enc_ver);
    g_free(esc);
    g_free(extra);
    return url;
}

char *uv_captcha_build_url(const UvCaptchaChallenge *challenge)
{
    return uv_captcha_build_url_for_lang(challenge, NULL);
}

bool uv_captcha_token_valid(const char *token)
{
    if (!token || !*token) return false;
    size_t len = strlen(token);
    if (len > UV_CAPTCHA_TOKEN_MAX) return false;
    return is_colon_field(token);
}

/* ------------------------------------------------------------------ *
 * GTK dialog (compiled only when WebKit support is detected)         *
 * ------------------------------------------------------------------ */

#ifdef UV_HAVE_WEBKIT

#include <webkit2/webkit2.h>
#include <jsc/jsc.h>

/* The Windows client embeds the page in a browser control that exposes a
 * "window.external" scripting object. The page reports the solved captcha
 * through window.external.FromJS_CaptchaCorrect(_, token). This stand-in
 * proxies every call into the host as "name<US>arg0<US>arg1..." (US = 0x1F). */
#define UV_CAPTCHA_MSG_HANDLER "remmina_uv_captcha"

static const char *captcha_bridge_js =
    "(function () {\n"
    "  var post = function (parts) {\n"
    "    window.webkit.messageHandlers." UV_CAPTCHA_MSG_HANDLER ".postMessage(parts.join('\\u001f'));\n"
    "  };\n"
    "  var external = new Proxy({}, {\n"
    "    get: function (_, name) {\n"
    "      if (typeof name !== 'string') { return undefined; }\n"
    "      return function () {\n"
    "        var parts = [name];\n"
    "        for (var i = 0; i < arguments.length; i++) {\n"
    "          parts.push(arguments[i] == null ? '' : String(arguments[i]));\n"
    "        }\n"
    "        post(parts);\n"
    "      };\n"
    "    },\n"
    "    has: function () { return true; }\n"
    "  });\n"
    "  try { Object.defineProperty(window, 'external', { value: external, configurable: true }); } catch (e) {}\n"
    "})();\n";

typedef struct {
    gint ref_count;
    gpointer parent_widget;
    gpointer owner;
    char *url;

    GMutex lock;
    GCond cond;
    bool done;
    bool got_token;
    char token[UV_CAPTCHA_TOKEN_MAX + 1];

    GtkWidget *dialog;
} UvCaptchaUi;

/* Registry of open captcha windows, one per owning connection, so two
 * Remmina tabs hitting a captcha at once do not clobber each other. */
static GMutex active_captcha_lock;
static GList *active_captcha_list = NULL;

static void captcha_ui_unref(UvCaptchaUi *ui)
{
    if (!ui) return;
    if (g_atomic_int_dec_and_test(&ui->ref_count)) {
        g_free(ui->url);
        g_mutex_clear(&ui->lock);
        g_cond_clear(&ui->cond);
        g_free(ui);
    }
}

static void captcha_store_token(UvCaptchaUi *ui, const char *token)
{
    GtkWidget *dlg = NULL;
    g_mutex_lock(&ui->lock);
    if (!ui->got_token && uv_captcha_token_valid(token)) {
        g_strlcpy(ui->token, token, sizeof(ui->token));
        ui->got_token = true;
    }
    dlg = ui->dialog;
    g_mutex_unlock(&ui->lock);

    if (dlg) {
        gtk_dialog_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    }
}

static void captcha_on_script_message(WebKitUserContentManager *manager,
                                      WebKitJavascriptResult *result,
                                      gpointer user_data)
{
    (void)manager;
    UvCaptchaUi *ui = (UvCaptchaUi *)user_data;

    JSCValue *value = webkit_javascript_result_get_js_value(result);
    if (!value) return;
    char *text = jsc_value_to_string(value);
    if (!text) return;
    char **parts = g_strsplit(text, "\x1F", 0);
    g_free(text);

    if (parts && parts[0] && g_strcmp0(parts[0], "FromJS_CaptchaCorrect") == 0) {
        /* The desktop reads the second argument (_, token). */
        char *raw_token = (parts[1] && parts[2]) ? g_strstrip(parts[2]) : NULL;
        if (raw_token && uv_captcha_token_valid(raw_token)) {
            captcha_store_token(ui, raw_token);
        } else {
            g_warning("UltraViewer captcha page sent an unusable token (%u bytes)",
                      raw_token ? (guint)strlen(raw_token) : 0u);
        }
    } else {
        g_debug("UltraViewer captcha page called %s", parts && parts[0] ? parts[0] : "(unknown)");
    }

    g_strfreev(parts);
}

static gboolean captcha_on_load_failed(WebKitWebView *view, WebKitLoadEvent event,
                                       gchar *failing_uri, GError *error, gpointer user_data)
{
    (void)view; (void)event;
    GtkWidget *error_label = GTK_WIDGET(user_data);
    g_warning("UltraViewer captcha page %s failed to load: %s", failing_uri,
              error ? error->message : "unknown error");
    gtk_label_set_text(GTK_LABEL(error_label),
                       _("The UltraViewer verification page could not be loaded. Check the connection and try again."));
    gtk_widget_show(error_label);
    return TRUE;
}

/* Links that ask for a new window stay in the same view. */
static WebKitWebView *captcha_on_create(WebKitWebView *view, WebKitNavigationAction *action,
                                        gpointer user_data)
{
    (void)user_data;
    WebKitURIRequest *request = webkit_navigation_action_get_request(action);
    const char *uri = request ? webkit_uri_request_get_uri(request) : NULL;
    if (uri) {
        webkit_web_view_load_uri(view, uri);
    }
    return NULL;
}

static void captcha_on_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    (void)response_id;
    (void)user_data;
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void captcha_on_dialog_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    UvCaptchaUi *ui = (UvCaptchaUi *)user_data;

    g_mutex_lock(&active_captcha_lock);
    active_captcha_list = g_list_remove(active_captcha_list, ui);
    g_mutex_unlock(&active_captcha_lock);

    g_mutex_lock(&ui->lock);
    ui->dialog = NULL;
    ui->done = true;
    g_cond_signal(&ui->cond);
    g_mutex_unlock(&ui->lock);

    captcha_ui_unref(ui);
}

static gboolean captcha_dialog_idle(gpointer user_data)
{
    UvCaptchaUi *ui = (UvCaptchaUi *)user_data;

    g_mutex_lock(&ui->lock);
    if (ui->done) {
        g_mutex_unlock(&ui->lock);
        captcha_ui_unref(ui);
        return G_SOURCE_REMOVE;
    }
    g_mutex_unlock(&ui->lock);

    GtkWidget *parent = NULL;
    if (ui->parent_widget && GTK_IS_WIDGET(ui->parent_widget)) {
        GtkWidget *top = gtk_widget_get_toplevel(GTK_WIDGET(ui->parent_widget));
        if (GTK_IS_WINDOW(top)) {
            parent = top;
        }
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        _("UltraViewer Verification"), parent ? GTK_WINDOW(parent) : NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 540, 700);

    GtkWidget *hint = gtk_label_new(_(
        "UltraViewer needs to verify this computer before it lets it connect.\n"
        "Type the characters shown on the page. When the page accepts them,\n"
        "this window closes and the connection resumes automatically."));
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_widget_set_margin_start(hint, 12);
    gtk_widget_set_margin_end(hint, 12);
    gtk_widget_set_margin_top(hint, 8);

    GtkWidget *error_label = gtk_label_new(NULL);
    gtk_label_set_line_wrap(GTK_LABEL(error_label), TRUE);
    gtk_widget_set_halign(error_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(error_label, 12);
    gtk_widget_set_margin_end(error_label, 12);
    gtk_widget_set_no_show_all(error_label, TRUE);

    WebKitUserContentManager *content = webkit_user_content_manager_new();
    /* Connect the handler and register it before creating the WebView. */
    g_signal_connect(content, "script-message-received::" UV_CAPTCHA_MSG_HANDLER,
                     G_CALLBACK(captcha_on_script_message), ui);
    webkit_user_content_manager_register_script_message_handler(content, UV_CAPTCHA_MSG_HANDLER);

    WebKitUserScript *script = webkit_user_script_new(
        captcha_bridge_js,
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        NULL, NULL);
    webkit_user_content_manager_add_script(content, script);
    webkit_user_script_unref(script);

    WebKitWebContext *context = webkit_web_context_new_ephemeral();
    WebKitWebView *view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                                                       "web-context", context,
                                                       "user-content-manager", content,
                                                       NULL));
    g_object_unref(context);
    g_object_unref(content);

    gtk_widget_set_size_request(GTK_WIDGET(view), 480, 560);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(view));
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), error_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    g_signal_connect(view, "load-failed", G_CALLBACK(captcha_on_load_failed), error_label);
    g_signal_connect(view, "create", G_CALLBACK(captcha_on_create), NULL);
    g_signal_connect(dialog, "response", G_CALLBACK(captcha_on_dialog_response), ui);
    g_signal_connect(dialog, "destroy", G_CALLBACK(captcha_on_dialog_destroy), ui);

    g_mutex_lock(&ui->lock);
    ui->dialog = dialog;
    g_mutex_unlock(&ui->lock);

    webkit_web_view_load_uri(view, ui->url);
    gtk_widget_show_all(dialog);

    return G_SOURCE_REMOVE;
}

char *uv_captcha_request_token(const UvCaptchaChallenge *challenge, gpointer parent_widget)
{
    if (!challenge) return NULL;

    UvCaptchaUi *ui = g_new0(UvCaptchaUi, 1);
    ui->ref_count = 2; /* 1 for worker thread, 1 for main-loop idle / dialog */
    ui->parent_widget = parent_widget;
    ui->owner = parent_widget;
    ui->url = uv_captcha_build_url(challenge);
    g_mutex_init(&ui->lock);
    g_cond_init(&ui->cond);

    g_mutex_lock(&active_captcha_lock);
    active_captcha_list = g_list_prepend(active_captcha_list, ui);
    g_mutex_unlock(&active_captcha_lock);

    g_main_context_invoke(NULL, captcha_dialog_idle, ui);

    g_mutex_lock(&ui->lock);
    while (!ui->done) {
        g_cond_wait(&ui->cond, &ui->lock);
    }
    char *token = ui->got_token ? g_strdup(ui->token) : NULL;
    g_mutex_unlock(&ui->lock);

    g_mutex_lock(&active_captcha_lock);
    active_captcha_list = g_list_remove(active_captcha_list, ui);
    g_mutex_unlock(&active_captcha_lock);

    captcha_ui_unref(ui);
    return token;
}

void uv_captcha_cancel_dialog(gpointer owner)
{
    GList *dialogs = NULL;

    g_mutex_lock(&active_captcha_lock);
    for (GList *l = active_captcha_list; l; ) {
        GList *next = l->next;
        UvCaptchaUi *ui = (UvCaptchaUi *)l->data;
        if (ui->owner == owner) {
            g_mutex_lock(&ui->lock);
            if (ui->dialog) {
                dialogs = g_list_prepend(dialogs, ui->dialog);
                ui->dialog = NULL;
            }
            ui->done = true;
            g_cond_signal(&ui->cond);
            g_mutex_unlock(&ui->lock);
            active_captcha_list = g_list_delete_link(active_captcha_list, l);
        }
        l = next;
    }
    g_mutex_unlock(&active_captcha_lock);

    for (GList *l = dialogs; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(dialogs);
}

#else /* !UV_HAVE_WEBKIT */

char *uv_captcha_request_token(const UvCaptchaChallenge *challenge, gpointer parent_widget)
{
    (void)challenge;
    (void)parent_widget;
    g_warning("UltraViewer asks for a captcha, but this build has no WebKit support. "
              "Rebuild the plugin with webkit2gtk-4.1 installed.");
    return NULL;
}

void uv_captcha_cancel_dialog(gpointer owner)
{
    (void)owner;
}

#endif /* UV_HAVE_WEBKIT */
