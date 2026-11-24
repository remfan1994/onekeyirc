/* CAP LS values are authoritative in their final left-to-right occurrence.
 * This fixture uses only the deterministic application transport and a fake
 * credential. It never opens a socket or reads a real configuration. */
#define main legacy_app_test_main
#include "test_app.c"
#undef main

static unsigned checks, failures;

static App *sasl_profile(int bouncer)
{
    Config c;
    memset(&c, 0, sizeof c);
    strcpy(c.server, "localhost"); c.port = 6697; c.tls = 1;
    strcpy(c.nick, "tester"); strcpy(c.username, "tester");
    strcpy(c.realname, "CAP final-value regression");
    strcpy(c.auth, "sasl"); strcpy(c.password_file, "/fake/cap-password");
    c.history_lines = 50; c.program_messages = 1;
    if (bouncer) {
        c.bouncer_type = CONFIG_BOUNCER_SOJU;
        strcpy(c.bouncer_user, "account"); strcpy(c.bouncer_network, "ircnow");
        strcpy(c.bouncer_client, "desktop");
    } else strcpy(c.auth_user, "account");
    App *a = calloc(1, sizeof *a);
    assert(a && app_init(a, &c, "/fake/config.ini"));
    assert(app_connect(a)); app_network(a, 0);
    assert(a->net); output_clear(a);
    return a;
}

static void final_value_case(int bouncer, const char *name,
                             const char *continuation, const char *final,
                             int expected_plain)
{
    App *a = sasl_profile(bouncer);
    char wire[1024];
    int before = secret_reads;
    if (continuation) {
        int n = snprintf(wire, sizeof wire, ":server CAP tester LS * :%s\r\n", continuation);
        assert(n > 0 && (size_t)n < sizeof wire);
        feed(a, wire);
        assert(a->net && !a->cap_pending && !a->auth_started);
    }
    int n = snprintf(wire, sizeof wire, ":server CAP tester LS :%s\r\n", final);
    assert(n > 0 && (size_t)n < sizeof wire);
    feed(a, wire);
    int requested = a->net && strstr(a->net->output, "CAP REQ :sasl") != NULL;
    ++checks;
    if (requested != expected_plain || (!expected_plain && a->net) ||
        secret_reads != before) {
        fprintf(stderr, "FAIL profile=%s case=%s expected_plain=%d requested=%d connected=%d credential_reads=%d\n",
                bouncer ? "soju" : "direct", name, expected_plain,
                requested, a->net != NULL, secret_reads - before);
        ++failures;
    } else if (expected_plain) {
        feed(a, ":server CAP tester ACK :sasl\r\nAUTHENTICATE +\r\n");
        assert(a->net && a->auth_sent && secret_reads == before + 1);
        feed(a, ":server 903 tester :Authenticated\r\n");
        assert(a->authenticated && a->cap_done);
    }
    destroy(a);
}

int main(void)
{
    strcpy(test_password, "FAKE_CAP_CANARY_7719");
    for (int bouncer = 0; bouncer <= 1; ++bouncer) {
        final_value_case(bouncer, "single-revoked", NULL, "sasl=PLAIN sasl=EXTERNAL", 0);
        final_value_case(bouncer, "single-empty", NULL, "sasl=PLAIN sasl=", 0);
        final_value_case(bouncer, "bare-then-revoked", NULL, "sasl sasl=EXTERNAL", 0);
        final_value_case(bouncer, "single-granted", NULL, "sasl=EXTERNAL sasl=PLAIN", 1);
        final_value_case(bouncer, "multiline-revoked", "sasl=PLAIN", "sasl=EXTERNAL", 0);
        final_value_case(bouncer, "multiline-empty", "sasl=PLAIN", "sasl=", 0);
        final_value_case(bouncer, "multiline-granted", "sasl=EXTERNAL", "sasl=PLAIN", 1);
        final_value_case(bouncer, "multiline-preserved", "sasl=PLAIN", "unrelated", 1);
        final_value_case(bouncer, "empty-then-bare", "sasl=", "sasl", 1);
        final_value_case(bouncer, "exact-mechanism", NULL, "sasl=PLAINX,XPLAIN,EXTERNAL", 0);
        final_value_case(bouncer, "mixed-mechanisms", NULL, "sasl=EXTERNAL,PLAIN,SCRAM-SHA-256", 1);
        final_value_case(bouncer, "trailing-space", NULL, "sasl=EXTERNAL sasl=PLAIN   ", 1);
    }
    printf("CAP SASL final-value regression: %u cases, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
