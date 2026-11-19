/* Deterministic hostile wire input through the real App receive path. */
#define main legacy_app_test_main
#include "test_app.c"
#undef main

static uint32_t next_random(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static App *connected_fixture(void)
{
    App *a = fresh(0);
    register_plain(a);
    feed(a, ":tester!user@local JOIN #test\r\n");
    output_clear(a);
    return a;
}

static void bounded_state(const App *a)
{
    size_t total = 0;
    assert(a->buffer_count <= APP_BUFFERS);
    assert(a->chat_count <= APP_QUEUE && a->control_count <= APP_QUEUE);
    assert(a->incoming_len <= IRC_WIRE_MAX);
    assert(a->list.count <= APP_LIST_MAX && a->list.count <= a->list.capacity);
    assert(a->log_bytes <= APP_LOG_BYTES);
    for (size_t i = 0; i < a->buffer_count; ++i) {
        const Buffer *b = &a->buffers[i];
        assert(b->line_count <= b->line_cap);
        assert(b->user_count <= APP_USERS && b->user_count <= b->user_cap);
        for (size_t j = 0; j < b->line_count; ++j) {
            const char *line = b->lines[(b->line_start + j) % b->line_cap];
            char sanitized[4096];
            size_t bytes = strlen(line);
            assert(bytes < sizeof sanitized);
            irc_sanitize(sanitized, sizeof sanitized, line);
            assert(!strcmp(line, sanitized));
            total += bytes + 1;
        }
    }
    assert(total == a->log_bytes);
}

int main(void)
{
    static const char *const seeds[] = {
        "PING :ping", "PONG :pong", ":server CAP tester LS * :sasl=PLAIN,EXTERNAL multi-prefix",
        ":server CAP tester ACK :sasl multi-prefix", "AUTHENTICATE +", ":server 903 tester :ok",
        ":server 001 tester :welcome", ":server 005 tester CASEMAPPING=ascii PREFIX=(qaohv)~&@%+ CHANMODES=b,k,l,imnst LINELEN=512 :supported",
        ":server 302 tester :tester=+user@host", ":server 396 tester host :host changed",
        ":server 305 tester :back", ":server 306 tester :away", ":server 311 tester bob user host * :real name",
        ":server 317 tester bob 18446744073709551615 0 :idle", ":server 321 tester :channel list",
        ":server 322 tester #test 18446744073709551615 :topic", ":server 323 tester :done",
        ":server 324 tester #test +k secret-key", ":server 332 tester #test :topic",
        ":server 333 tester #test bob 18446744073709551615", ":server 353 tester = #test :@tester +bob carol",
        ":server 366 tester #test :end", ":bob!user@host PRIVMSG tester :hello",
        "@time=2026-11-15T12:34:56.000Z;label=fixture :bob!user@host PRIVMSG #test :tagged message",
        ":bob!user@host NOTICE tester :notice", ":bob!user@host PRIVMSG #test :\001ACTION waves\001",
        ":bob!user@host PRIVMSG tester :\001DCC SEND file 2130706433 1234 10\001",
        ":NickServ!service@host NOTICE tester :token", ":bob!user@host JOIN #test",
        ":bob!user@host PART #test :reason", ":bob!user@host NICK :carol",
        ":bob!user@host QUIT :reason", ":bob!user@host MODE #test +ov-k tester carol secret-key",
        ":bob!user@host KICK #test tester :reason", ":bob!user@host INVITE tester #new",
        ":bob!user@host TOPIC #test :new topic", "ERROR :remote shutdown"
    };
    uint32_t random = 0x76312425u;
    unsigned resets = 0;
    App *a = connected_fixture();
    for (size_t step = 0; step < 40000; ++step) {
        unsigned char wire[IRC_WIRE_MAX + 4];
        const char *seed = seeds[next_random(&random) % (sizeof seeds / sizeof seeds[0])];
        size_t length = strlen(seed);
        memcpy(wire, seed, length);
        switch (step % 6u) {
        case 0: /* Preserve valid control and membership events. */
            break;
        case 1: /* Prefix truncations stay within the wire buffer. */
            length = next_random(&random) % (length + 1u);
            break;
        case 2:
            for (unsigned i = 0; i < 1u + next_random(&random) % 8u; ++i)
                wire[next_random(&random) % length] = (unsigned char)(next_random(&random) >> 24);
            break;
        case 3: /* Arbitrary suffix bytes exercise framing too. */
            length += next_random(&random) % 512u;
            for (size_t i = strlen(seed); i < length; ++i)
                wire[i] = (unsigned char)(next_random(&random) >> 24);
            break;
        case 4:
            length = next_random(&random) % 900u;
            for (size_t i = 0; i < length; ++i)
                wire[i] = (unsigned char)(1u + next_random(&random) % 255u);
            break;
        default: /* Exercise the exact oversized-line boundary periodically. */
            if (step % 601u == 0) {
                length = IRC_WIRE_MAX + 1u;
                memset(wire, 'x', length);
            }
            break;
        }
        wire[length++] = '\r';
        wire[length++] = '\n';
        feed_bytes(a, wire, length);
        if (!(step % 64u) || !a->net) bounded_state(a);
        if (!a->net) {
            destroy(a);
            a = connected_fixture();
            ++resets;
        } else output_clear(a);
    }
    bounded_state(a);
    destroy(a);
    assert(!secret_reads && !credential_starts && !config_saves);
    printf("hostile application state: 40000 deterministic wire cases; %u bounded disconnect/reset outcomes; history, roster, list, queue and sanitization invariants passed\n", resets);
    return 0;
}
