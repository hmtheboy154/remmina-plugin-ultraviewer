/*
 * UltraViewer Protocol Plugin - Unit Tests
 */

#include "uv_types.h"
#include "uv_crypto.h"
#include "uv_wire.h"
#include "uv_input.h"
#include "uv_clipboard.h"
#include "uv_command.h"
#include "uv_cursor.h"
#include "uv_audio.h"
#include "uv_file_transfer.h"
#include "uv_screen.h"

void uv_session_queue_outbound(UvSession *session, bool format_flag, const uint8_t *payload, size_t len)
{
    (void)session;
    (void)format_flag;
    (void)payload;
    (void)len;
}
#include <assert.h>
#include <stdio.h>

static void unhex(const char *hex_str, uint8_t *out_bytes, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        unsigned int val = 0;
        sscanf(hex_str + (i * 2), "%02x", &val);
        out_bytes[i] = (uint8_t)val;
    }
}

static void test_crypto_vectors(void)
{
    printf("[*] Testing 3DES command cipher vectors...\n");

    const char *pwd = "fixture-⌘";
    const char *plain = "8:123456789:synthetic-proof";
    const char *expected_hex = "b63b9797064974fa31181fef729c755558492379dcb44c69e3fe4c6d0e29836c";

    uint8_t *cipher = NULL;
    size_t cipher_len = 0;
    bool ok = uv_custom_cipher_encrypt(pwd, (const uint8_t *)plain, strlen(plain), &cipher, &cipher_len);
    assert(ok && cipher_len == 32);

    uint8_t expected_bytes[32];
    unhex(expected_hex, expected_bytes, 32);
    assert(memcmp(cipher, expected_bytes, 32) == 0);

    uint8_t *decrypted = NULL;
    size_t dec_len = 0;
    ok = uv_custom_cipher_decrypt(pwd, cipher, cipher_len, &decrypted, &dec_len);
    assert(ok && dec_len == strlen(plain));
    assert(memcmp(decrypted, plain, dec_len) == 0);

    g_free(cipher);
    g_free(decrypted);

    /* Server Cipher vector: "8:fixture" -> c842551785856d0b8f866afed8dcd8b1 */
    const char *srv_plain = "8:fixture";
    const char *srv_expected_hex = "c842551785856d0b8f866afed8dcd8b1";
    uint8_t srv_expected[16];
    unhex(srv_expected_hex, srv_expected, 16);

    uint8_t *srv_cipher = NULL;
    size_t srv_cipher_len = 0;
    ok = uv_server_cipher_encrypt((const uint8_t *)srv_plain, strlen(srv_plain), &srv_cipher, &srv_cipher_len);
    assert(ok && srv_cipher_len == 16);
    assert(memcmp(srv_cipher, srv_expected, 16) == 0);
    g_free(srv_cipher);

    /* Password proof vector: nonce="12345678", pw="test-pass" */
    char proof2[33];
    uv_md5_hex("12345678test-pass", proof2);
    assert(strcmp(proof2, "c10f3394b41ec5b86060e0285e0dd306") == 0);

    char proof13[33];
    uv_md5_hex("                                test-pass", proof13);
    assert(strcmp(proof13, "85347ba7a403ffed52d311b7c5b6b549") == 0);

    printf("[✓] Crypto vectors verified.\n");
}

static void test_relay_envelopes(void)
{
    printf("[*] Testing relay envelopes...\n");

    /* Forward envelope: 192.0.2.10:12345, format_flag=0, payload=[0xAA, 0xBB] */
    uint8_t payload[] = { 0xAA, 0xBB };
    uint8_t *env = NULL;
    size_t env_len = 0;
    bool ok = uv_forward_envelope_encode("192.0.2.10", 12345, false, payload, 2, &env, &env_len);
    assert(ok && env_len == 10);

    uint8_t expected[] = { 0x00, 0x00, 0x0A, 0x02, 0x00, 0xC0, 0x39, 0x30, 0xAA, 0xBB };
    assert(memcmp(env, expected, 10) == 0);

    UvEnvelope decoded;
    ok = uv_envelope_decode(env, env_len, &decoded);
    assert(ok);
    assert(decoded.type == UV_ENVELOPE_FORWARD);
    assert(decoded.format_flag == false);
    assert(strcmp(decoded.ip, "192.0.2.10") == 0);
    assert(decoded.port == 12345);
    assert(decoded.payload_len == 2);
    assert(decoded.payload[0] == 0xAA && decoded.payload[1] == 0xBB);
    g_free(env);

    /* System envelope: "ping" */
    ok = uv_system_envelope_encode((const uint8_t *)"ping", 4, false, &env, &env_len);
    assert(ok && env_len == 5);
    assert(env[0] == 0x01 && memcmp(env + 1, "ping", 4) == 0);
    g_free(env);

    printf("[✓] Relay envelopes verified.\n");
}

static void test_multipart(void)
{
    printf("[*] Testing multipart framing...\n");

    uint8_t part0[] = { 0x01 };
    uint8_t part1[] = { 0x00, 0x01, 0x02 };
    UvPart parts[2] = {
        { part0, sizeof(part0) },
        { part1, sizeof(part1) }
    };

    uint8_t *mp = NULL;
    size_t mp_len = 0;
    bool ok = uv_multipart_encode(parts, 2, &mp, &mp_len);
    assert(ok && mp_len == (4 + 1 + 4 + 3));

    GArray *decoded_parts = NULL;
    ok = uv_multipart_decode(mp, mp_len, &decoded_parts);
    assert(ok && decoded_parts->len == 2);

    const UvPart *d0 = &g_array_index(decoded_parts, UvPart, 0);
    const UvPart *d1 = &g_array_index(decoded_parts, UvPart, 1);
    assert(d0->len == 1 && d0->data[0] == 0x01);
    assert(d1->len == 3 && memcmp(d1->data, part1, 3) == 0);

    g_array_free(decoded_parts, TRUE);
    g_free(mp);

    printf("[✓] Multipart framing verified.\n");
}

static void test_input_encoding(void)
{
    printf("[*] Testing input encoding...\n");

    uint8_t *move_payload = NULL;
    size_t move_len = 0;
    bool ok = uv_encode_mouse_move(100, 200, 1, &move_payload, &move_len);
    assert(ok);
    assert(strcmp((char *)move_payload, "8|||512|||1|||100|||200") == 0);
    g_free(move_payload);

    uint8_t *key_payload = NULL;
    size_t key_len = 0;
    ok = uv_encode_key(GDK_KEY_a, 0, true, &key_payload, &key_len);
    assert(ok && key_len == 11);
    assert(key_payload[0] == 30 && key_payload[1] == '|' && key_payload[4] == UV_KEY_DOWN);
    assert(key_payload[5] == 0x41); /* VK_A */
    assert(key_payload[6] == 0x1E && key_payload[7] == 0); /* Scan code */
    g_free(key_payload);

    printf("[✓] Input encoding verified.\n");
}

static void test_clipboard(void)
{
    printf("[*] Testing clipboard encoding...\n");

    const char *text = "Remmina UltraViewer Clipboard Test";
    uint8_t *payload = NULL;
    size_t len = 0;
    bool format_flag = false;
    bool ok = uv_clipboard_encode_viewer_to_host(text, &payload, &len, &format_flag);
    assert(ok && format_flag == false);
    assert(g_str_has_prefix((char *)payload, "33|||"));
    g_free(payload);

    printf("[✓] Clipboard encoding verified.\n");
}

static void test_audio_g722(void)
{
    printf("[*] Testing G.722 audio reference decoder...\n");

    uint8_t input[39];
    for (int i = 0; i <= 38; i++) input[i] = (uint8_t)i;
    int16_t output[78];
    size_t count = uv_g722_decode(input, 39, output, 78);
    assert(count == 78);
    assert(output[76] == -28446);

    printf("[✓] G.722 reference decoder verified.\n");
}

static void test_shortcuts_and_clipboard_cmd5(void)
{
    printf("[*] Testing keyboard shortcuts (Ctrl+A/Ctrl+V) & multipart clipboard 5...\n");

    /* Shortcut: Ctrl+A -> vk=0x41, scan=0x1E, ch=0 */
    uint8_t *ctrl_a = NULL;
    size_t ca_len = 0;
    bool ok = uv_encode_key(GDK_KEY_a, GDK_CONTROL_MASK, true, &ctrl_a, &ca_len);
    assert(ok && ca_len == 11);
    assert(ctrl_a[5] == 0x41);
    assert(uv_read_u16_le(ctrl_a + 6) == 0x1E);
    assert(uv_read_u16_le(ctrl_a + 8) == 0); /* Suppressed character for shortcut */
    g_free(ctrl_a);

    /* Normal 'a' -> vk=0x41, scan=0x1E, ch='a' */
    uint8_t *plain_a = NULL;
    size_t pa_len = 0;
    ok = uv_encode_key(GDK_KEY_a, 0, true, &plain_a, &pa_len);
    assert(ok && pa_len == 11);
    assert(plain_a[5] == 0x41);
    assert(uv_read_u16_le(plain_a + 8) == 'a');
    g_free(plain_a);

    /* Command 5 multipart clipboard decode */
    uint8_t *utf16 = NULL;
    size_t utf16_len = 0;
    ok = uv_utf8_to_utf16le("Hello from Partner", &utf16, &utf16_len);
    assert(ok);

    uint8_t c5 = 5;
    UvPart parts[2] = {
        { &c5, 1 },
        { utf16, utf16_len }
    };
    GArray *arr = g_array_new(FALSE, FALSE, sizeof(UvPart));
    g_array_append_vals(arr, parts, 2);

    char *decoded_text = uv_clipboard_decode_host_to_viewer(arr);
    assert(decoded_text != NULL);
    assert(strcmp(decoded_text, "Hello from Partner") == 0);
    g_free(decoded_text);
    g_array_free(arr, TRUE);
    g_free(utf16);

    printf("[✓] Shortcuts & clipboard Command 5 verified.\n");
}

static void test_cursor_handling(void)
{
    printf("[*] Testing cursor selection shape requests & position decoding...\n");

    UvCursorManager *mgr = uv_cursor_manager_new();
    assert(mgr != NULL);

    /* Test Command 3: cache miss triggers shape request with missing hash */
    uint8_t c3 = 3;
    uint8_t hash_bytes[4];
    uv_write_u32_le(hash_bytes, 0x12345678);
    UvPart parts3[2] = {
        { &c3, 1 },
        { hash_bytes, 4 }
    };
    GArray *arr3 = g_array_new(FALSE, FALSE, sizeof(UvPart));
    g_array_append_vals(arr3, parts3, 2);

    GdkCursor *cur = NULL;
    int32_t missing_hash = 0;
    bool found = uv_cursor_handle_selection(mgr, arr3, &cur, &missing_hash);
    assert(!found);
    assert(cur == NULL);
    assert(missing_hash == 0x12345678);

    char req[32];
    snprintf(req, sizeof(req), "4|||%d", missing_hash);
    assert(strcmp(req, "4|||305419896") == 0);

    /* Test Ignored Selection Hash (0xb84d1a70) */
    uv_write_u32_le(hash_bytes, (uint32_t)UV_IGNORED_SELECTION_HASH);
    found = uv_cursor_handle_selection(mgr, arr3, &cur, &missing_hash);
    assert(found);
    assert(cur == NULL);
    assert(missing_hash == 0);
    g_array_free(arr3, TRUE);

    /* Test Command 28: Cursor Position */
    uint8_t c28 = 28;
    uint8_t x_bytes[2], y_bytes[2];
    uv_write_u16_le(x_bytes, (uint16_t)850);
    uv_write_u16_le(y_bytes, (uint16_t)-420);
    UvPart parts28[3] = {
        { &c28, 1 },
        { x_bytes, 2 },
        { y_bytes, 2 }
    };
    GArray *arr28 = g_array_new(FALSE, FALSE, sizeof(UvPart));
    g_array_append_vals(arr28, parts28, 3);

    int16_t out_x = 0, out_y = 0;
    bool pos_ok = uv_cursor_handle_position(mgr, arr28, &out_x, &out_y);
    assert(pos_ok);
    assert(out_x == 850);
    assert(out_y == -420);
    assert(mgr->remote_x == 850);
    assert(mgr->remote_y == -420);
    g_array_free(arr28, TRUE);

    uv_cursor_manager_free(mgr);

    printf("[✓] Cursor handling and position decoding verified.\n");
}

static void test_registration_reply_parsing(void)
{
    printf("[*] Testing registration reply parsing...\n");

    UvClientIdentity ident;
    memset(&ident, 0, sizeof(ident));
    char *err_msg = NULL;

    const char *ok_reg = "3:443:00111222333:203.0.113.7:unknown:1:ping-list:0:17/10/2026:relay.example:opaque-token:future:";
    bool ok = uv_parse_registration_reply(ok_reg, &ident, &err_msg);
    assert(ok);
    assert(ident.public_port == 443);
    assert(strcmp(ident.assigned_id, "111222333") == 0);
    assert(strcmp(ident.public_ip, "203.0.113.7") == 0);
    assert(strcmp(ident.server_token, "opaque-token") == 0);
    assert(err_msg == NULL);

    /* Non-"3:" frame (e.g. "60:...") is rejected if passed directly to parser */
    ok = uv_parse_registration_reply("60:Q7V5Jf+8UFHUXkKYMPTL3k70A0Uv6q3sh7P4FStpsH8=:", &ident, &err_msg);
    assert(!ok);
    assert(err_msg != NULL);
    g_free(err_msg);

    printf("[✓] Registration reply parsing verified.\n");
}

static void test_lookup_reply_parsing(void)
{
    UvPartnerLookup lookup;
    char *err_msg = NULL;

    /* 1. Success vector */
    const char *ok_vector = "8:1:192.0.2.9:unused:203.0.113.9:54000:5900:5901:TmFtZQ==:55100:unused:198.51.100.2:-1:123:relay.example:x:x:x:x:x:secret:direct-info:42";
    bool ok = uv_parse_lookup_reply(ok_vector, "42", &lookup, &err_msg);
    assert(ok);
    assert(lookup.status == 1);
    assert(strcmp(lookup.partner_ip, "203.0.113.9") == 0);
    assert(lookup.target_port == 55100);
    assert(strcmp(lookup.relay_ip, "198.51.100.2") == 0);
    assert(strcmp(lookup.relay_hostname, "relay.example") == 0);
    assert(strcmp(lookup.secret_token, "secret") == 0);
    assert(strcmp(lookup.partner_name, "Name") == 0);
    assert(err_msg == NULL);

    /* 2. Wrong partner password vector (status 2, only 3 tokens) */
    const char *wrong_pw_vector = "8:2:42";
    ok = uv_parse_lookup_reply(wrong_pw_vector, "42", &lookup, &err_msg);
    assert(!ok);
    assert(lookup.status == 2);
    assert(err_msg != NULL);
    assert(strstr(err_msg, "rejected partner password") != NULL);
    g_free(err_msg);
    err_msg = NULL;

    /* 3. Partner offline vector (status 0) */
    const char *offline_vector = "8:0:42";
    ok = uv_parse_lookup_reply(offline_vector, "42", &lookup, &err_msg);
    assert(!ok);
    assert(lookup.status == 0);
    assert(err_msg != NULL);
    assert(strstr(err_msg, "offline") != NULL);
    g_free(err_msg);
    err_msg = NULL;

    /* 4. Partner offline vector (status -1) */
    const char *offline_neg_vector = "8:-1:42";
    ok = uv_parse_lookup_reply(offline_neg_vector, "42", &lookup, &err_msg);
    assert(!ok);
    assert(lookup.status == -1);
    assert(err_msg != NULL);
    assert(strstr(err_msg, "offline") != NULL);
    g_free(err_msg);
    err_msg = NULL;

    /* 5. Custom decline code vector (status 5) */
    const char *decline_vector = "8:5:42";
    ok = uv_parse_lookup_reply(decline_vector, "42", &lookup, &err_msg);
    assert(!ok);
    assert(lookup.status == 5);
    assert(err_msg != NULL);
    assert(strstr(err_msg, "code 5") != NULL);
    g_free(err_msg);
    err_msg = NULL;

    /* 6. Correlation mismatch vector */
    ok = uv_parse_lookup_reply("8:2:999", "42", &lookup, &err_msg);
    assert(!ok);
    assert(err_msg != NULL);
    assert(strstr(err_msg, "Correlation mismatch") != NULL);
    g_free(err_msg);
    err_msg = NULL;

    /* 7. Malformed reply */
    ok = uv_parse_lookup_reply("3:1:42", "42", &lookup, &err_msg);
    assert(!ok);
    assert(err_msg != NULL);
    assert(strstr(err_msg, "Malformed lookup reply") != NULL);
    g_free(err_msg);
    err_msg = NULL;

    /* 8. Incomplete status 1 reply (< 15 tokens) */
    ok = uv_parse_lookup_reply("8:1:192.0.2.9:42", "42", &lookup, &err_msg);
    assert(!ok);
    assert(err_msg != NULL);
    assert(strstr(err_msg, "Incomplete lookup routing details") != NULL);
    g_free(err_msg);
    err_msg = NULL;

    printf("[✓] Lookup reply parsing and error reporting verified.\n");
}

static void test_screen_reinit(void)
{
    printf("[*] Testing dynamic screen reinitialization...\n");

    /* Create initial 2736x1824 ScreenInit */
    UvScreenInit init1;
    memset(&init1, 0, sizeof(init1));
    init1.width = 2736;
    init1.height = 1824;
    init1.tile_size = 16;
    g_strlcpy(init1.version, "6.6.133", sizeof(init1.version));

    UvScreenRenderer *r = uv_screen_renderer_new(&init1, NULL);
    assert(r != NULL);
    assert(r->width == 2736);
    assert(r->height == 1824);
    assert(r->last_frame_number == UV_RESET_FRAME_NUMBER);
    assert(r->cairo_surface != NULL);

    /* Free and re-init to 1280x720 (simulating resolution change + refresh) */
    uv_screen_renderer_free(r);

    UvScreenInit init2;
    memset(&init2, 0, sizeof(init2));
    init2.width = 1280;
    init2.height = 720;
    init2.tile_size = 16;
    g_strlcpy(init2.version, "6.6.133", sizeof(init2.version));

    r = uv_screen_renderer_new(&init2, NULL);
    assert(r != NULL);
    assert(r->width == 1280);
    assert(r->height == 720);
    assert(r->last_frame_number == UV_RESET_FRAME_NUMBER);
    assert(r->cairo_surface != NULL);

    uv_screen_renderer_free(r);
    printf("[✓] Screen reinitialization verified.\n");
}

static bool mock_auth_request_cb(UvSession *session, char **out_new_password, gpointer user_data)
{
    (void)session;
    int *called = (int *)user_data;
    (*called)++;
    *out_new_password = g_strdup("correct_password");
    return true;
}

static void test_auth_retry_callback(void)
{
    printf("[*] Testing auth retry request callback...\n");

    int called = 0;
    char *new_pwd = NULL;
    bool ok = mock_auth_request_cb(NULL, &new_pwd, &called);
    assert(ok);
    assert(called == 1);
    assert(new_pwd != NULL);
    assert(strcmp(new_pwd, "correct_password") == 0);
    g_free(new_pwd);

    printf("[✓] Auth retry request callback verified.\n");
}

int main(void)
{
    printf("=========================================\n");
    printf("Running UltraViewer Plugin Test Suite\n");
    printf("=========================================\n");

    test_crypto_vectors();
    test_relay_envelopes();
    test_multipart();
    test_input_encoding();
    test_clipboard();
    test_audio_g722();
    test_shortcuts_and_clipboard_cmd5();
    test_cursor_handling();
    test_registration_reply_parsing();
    test_lookup_reply_parsing();
    test_screen_reinit();
    test_auth_retry_callback();

    printf("=========================================\n");
    printf("All Unit Tests Passed Successfully!\n");
    printf("=========================================\n");
    return 0;
}
