#include "uv_session.h"
#include "uv_crypto.h"
#include <assert.h>

enum Scenario {
    REUSE_CONNECTION, KEEPALIVE_FAILURE, REGISTRATION_FAILURE,
    REPEATED_FAILURE, CANCEL_CAPTCHA, STOP_SESSION
};
static enum Scenario scenario;
static int connects, closes, registrations, lookups, writes, closed_callbacks;
static unsigned int registration_connections[3];

UvTlsSocket *__wrap_uv_tls_connect(const char *host, uint16_t port,
                                   const char *expected_hostname, bool insecure, char **error)
{
    (void)host; (void)port; (void)expected_hostname; (void)error;
    assert(!insecure);
    UvTlsSocket *sock = g_new0(UvTlsSocket, 1);
    sock->fd = -1;
    sock->port = ++connects;
    return sock;
}

void __wrap_uv_tls_close(UvTlsSocket *sock)
{
    assert(sock);
    closes++;
    g_free(sock);
}

bool __wrap_uv_identity_init(UvClientIdentity *ident)
{
    memset(ident, 0, sizeof(*ident));
    return true;
}

bool __wrap_uv_tls_write_frame(UvTlsSocket *sock, const uint8_t *payload, size_t len)
{
    assert(sock && len > 2 && payload[0] == 1);
    uint8_t *plain = NULL;
    size_t plain_len = 0;
    assert(uv_server_cipher_decrypt(payload + 2, len - 2, &plain, &plain_len));
    assert(plain_len == 1 && plain[0] == '2');
    g_free(plain);
    writes++;
    return scenario != KEEPALIVE_FAILURE;
}

bool __wrap_uv_command_register(UvTlsSocket *sock, UvClientIdentity *ident,
                                const char *token, UvCaptchaChallenge *challenge, char **error)
{
    (void)ident;
    assert(registrations < 3);
    registration_connections[registrations++] = sock->port;
    memset(challenge, 0, sizeof(*challenge));
    if (registrations == 1) {
        assert(!token[0]);
        g_strlcpy(challenge->esc, "fixture-challenge", sizeof(challenge->esc));
        *error = g_strdup("Fixture verification required");
        return false;
    }
    assert(strcmp(token, "fixture-solved-token") == 0);
    if (((scenario == REGISTRATION_FAILURE || scenario == KEEPALIVE_FAILURE) && registrations == 2) ||
        scenario == REPEATED_FAILURE) {
        *error = g_strdup("Fixture connection closed");
        return false;
    }
    return true;
}

bool __wrap_uv_command_lookup(UvTlsSocket *sock, const UvClientIdentity *ident,
                              const char *id, const char *password,
                              UvPartnerLookup *lookup, char **error)
{
    (void)ident; (void)id; (void)password; (void)lookup;
    assert(sock);
    lookups++;
    *error = g_strdup("Fixture stops before partner lookup");
    return false;
}

static bool captcha_requested(UvSession *session, const UvCaptchaChallenge *challenge,
                               char **token, gpointer data)
{
    (void)data;
    assert(strcmp(challenge->esc, "fixture-challenge") == 0);
    if (scenario == STOP_SESSION) session->running = false;
    uv_session_captcha_keepalive(session);
    if (scenario == CANCEL_CAPTCHA) return false;
    *token = g_strdup("fixture-solved-token");
    return true;
}

static void session_closed(UvSession *session, const char *error, gpointer data)
{
    (void)session; (void)data;
    assert(error);
    closed_callbacks++;
}

static void test_registration(enum Scenario current)
{
    scenario = current;
    connects = closes = registrations = lookups = writes = closed_callbacks = 0;
    memset(registration_connections, 0, sizeof(registration_connections));
    uv_captcha_set_cached_token(NULL);
    UvSession session = {0};
    session.wake_pipe[0] = session.wake_pipe[1] = -1;
    session.on_captcha_request = captcha_requested;
    session.on_closed = session_closed;
    assert(uv_session_start(&session));
    g_thread_join(session.worker_thread);
    session.worker_thread = NULL;

    if (current == CANCEL_CAPTCHA || current == STOP_SESSION) {
        assert(registrations == 1 && connects == 1 && lookups == 0);
        assert(closed_callbacks == (current == CANCEL_CAPTCHA ? 1 : 0));
    } else if (current == REPEATED_FAILURE) {
        assert(registrations == 3 && connects == 2 && lookups == 0);
        assert(closed_callbacks == 1 && closes == 1);
    } else {
        assert(lookups == 1 && closed_callbacks == 1);
        assert(registration_connections[0] == 1);
        if (current == REUSE_CONNECTION) {
            assert(connects == 1 && registrations == 2);
            assert(registration_connections[1] == 1 && closes == 0);
        } else {
            assert(connects == 2 && registrations == 3);
            assert(registration_connections[1] == 1 && registration_connections[2] == 2);
            assert(closes == 1);
        }
    }
    assert(writes == (current == STOP_SESSION ? 0 : 1));
    if (session.cmd_sock) __wrap_uv_tls_close(session.cmd_sock);
    assert(closes == connects);
}

int main(void)
{
    test_registration(REUSE_CONNECTION);
    test_registration(KEEPALIVE_FAILURE);
    test_registration(REGISTRATION_FAILURE);
    test_registration(REPEATED_FAILURE);
    test_registration(CANCEL_CAPTCHA);
    test_registration(STOP_SESSION);
    return 0;
}
