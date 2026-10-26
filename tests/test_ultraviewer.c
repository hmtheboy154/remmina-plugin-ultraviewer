/*
 * UltraViewer Protocol Plugin - Unit Tests
 */

#include "uv_types.h"
#include "uv_crypto.h"
#include "uv_wire.h"
#include "uv_input.h"
#include "uv_clipboard.h"
#include "uv_command.h"
#include "uv_audio.h"
#include "uv_file_transfer.h"

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

    printf("=========================================\n");
    printf("All Unit Tests Passed Successfully!\n");
    printf("=========================================\n");
    return 0;
}
