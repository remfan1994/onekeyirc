/* Bounded deterministic model checks for hostile CAP and nested BATCH input.
 * All input/output uses test_app's in-memory transport. No public network,
 * external helper, real credential or DCC socket is used. */
#define main legacy_app_test_main
#include "test_app.c"
#undef main

static unsigned property_random = 0xc3a51f27u;
static unsigned offer_count;
static unsigned cap_runs, batch_steps, invalid_runs;

static unsigned next_random(void)
{
    property_random ^= property_random << 13;
    property_random ^= property_random >> 17;
    property_random ^= property_random << 5;
    return property_random;
}

static void cap_model(void)
{
    /* Independent semantic expectations, including absent, case-sensitive,
     * malformed and exact-mechanism tokens. -1 means no SASL declaration. */
    static const struct { const char *token; int plain; } choices[] = {
        {"sasl", 1}, {"sasl=", 0}, {"sasl=PLAIN", 1},
        {"sasl=EXTERNAL", 0}, {"sasl=EXTERNAL,PLAIN", 1},
        {"sasl=PLAINX", 0}, {"sasl=XPLAIN", 0},
        {"sasl=PLAIN,EXTERNAL", 1}, {"sasl=PLAIN;EXTERNAL", 0},
        {"sasl=plain", 0}, {"sasl=,PLAIN,", 1},
        {"sas", -1}, {"saslX", -1}, {"xsasl", -1},
        {"SASL=PLAIN", -1}, {"sasl2=PLAIN", -1}, {"other", -1}
    };
    for (unsigned run = 0; run < 1024; ++run) {
        App *a = fresh(1);
        int reads = secret_reads, expected = 0;
        unsigned lines = 1u + next_random() % 4u;
        output_clear(a);
        for (unsigned line = 0; line < lines; ++line) {
            char list[384] = "", wire[448];
            unsigned tokens = 1u + next_random() % 8u;
            for (unsigned token = 0; token < tokens; ++token) {
                unsigned selected = next_random() % (sizeof choices / sizeof choices[0]);
                if (choices[selected].plain >= 0) expected = choices[selected].plain;
                assert(strlen(list) + strlen(choices[selected].token) + 3 < sizeof list);
                strcat(list, (next_random() & 1u) ? " " : "  ");
                strcat(list, choices[selected].token);
            }
            strcat(list, " ");
            int n = snprintf(wire, sizeof wire, ":server CAP tester LS %s:%s\r\n",
                             line + 1 < lines ? "* " : "", list);
            assert(n > 0 && (size_t)n < sizeof wire);
            feed(a, wire);
            assert(secret_reads == reads && !a->auth_sent && !a->registered);
            if (line + 1 < lines) {
                assert(a->net && !strstr(a->net->output, "CAP REQ"));
            } else {
                assert((a->net != NULL) == expected);
                if (expected) assert(strstr(a->net->output, "CAP REQ :sasl\r\n"));
            }
        }
        destroy(a); ++cap_runs;
    }
}

static int offer_seen(void *context, const char *nick, const char *body)
{
    (void)context;
    assert(!strcmp(nick, "alice") && !strncmp(body, "DCC SEND ", 9));
    ++offer_count;
    return 1;
}

static App *batch_profile(void)
{
    App *a = fresh(0);
    /* Exercise restored bouncer membership without adding real authentication
     * or networking to the fixture. CAP/BATCH processing is shared. */
    a->cfg.bouncer_type = CONFIG_BOUNCER_ZNC;
    feed(a, ":server CAP tester LS :batch server-time\r\n"
            ":server CAP tester ACK :batch server-time\r\n"
            ":server 001 tester :Welcome\r\n"
            ":tester!u@host JOIN #test\r\n");
    assert(a->registered && a->cap_batch && a->cap_time);
    assert(a->buffers[3].joined);
    a->dcc_offer = offer_seen;
    output_clear(a);
    return a;
}

typedef struct ModelBatch { unsigned parent; int active, history; } ModelBatch;

static unsigned active_count(const ModelBatch *model)
{
    unsigned count = 0;
    for (unsigned i = 0; i < APP_BATCHES; ++i) count += model[i].active != 0;
    return count;
}

static unsigned active_at(const ModelBatch *model, unsigned nth)
{
    for (unsigned i = 0; i < APP_BATCHES; ++i)
        if (model[i].active && !nth--) return i;
    abort();
}

static int has_children(const ModelBatch *model, unsigned slot)
{
    for (unsigned i = 0; i < APP_BATCHES; ++i)
        if (model[i].active && model[i].parent == slot + 1) return 1;
    return 0;
}

static void check_model(const App *a, const ModelBatch *model)
{
    unsigned matched = 0;
    assert(a->net && a->registered && a->control_count <= APP_QUEUE && a->chat_count <= APP_QUEUE);
    assert(a->buffer_count <= APP_BUFFERS && a->log_bytes <= APP_LOG_BYTES);
    for (unsigned i = 0; i < APP_BATCHES; ++i) {
        char reference[32];
        snprintf(reference, sizeof reference, "Model-%u", i);
        const AppBatch *found = NULL;
        for (unsigned j = 0; j < APP_BATCHES; ++j)
            if (!strcmp(a->batches[j].reference, reference)) found = &a->batches[j];
        assert((found != NULL) == model[i].active);
        if (found) {
            assert(found->history == model[i].history);
            if (model[i].parent) {
                snprintf(reference, sizeof reference, "Model-%u", model[i].parent - 1u);
                assert(found->parent && found->parent <= APP_BATCHES);
                assert(!strcmp(a->batches[found->parent - 1].reference, reference));
            } else assert(!found->parent);
            ++matched;
        }
    }
    assert(matched == active_count(model));
}

static void batch_model(void)
{
    App *a = batch_profile();
    ModelBatch model[APP_BATCHES] = {{0}};
    for (unsigned step = 0; step < 8192; ++step) {
        char wire[512], tag[80] = "";
        unsigned count = active_count(model), operation = next_random() % 6u;
        if ((!count || operation == 0) && count < APP_BATCHES) {
            unsigned slot;
            for (slot = 0; model[slot].active; ++slot) {}
            unsigned parent = count && next_random() % 3u ? active_at(model, next_random() % count) + 1u : 0u;
            int history = (next_random() & 1u) != 0;
            if (parent) snprintf(tag, sizeof tag, "@batch=Model-%u ", parent - 1u);
            int n = snprintf(wire, sizeof wire, "%s:server BATCH +Model-%u %s #test\r\n",
                             tag, slot, history ? "chathistory" : "vendor.example/live");
            assert(n > 0 && (size_t)n < sizeof wire);
            feed(a, wire);
            model[slot].active = 1; model[slot].parent = parent;
            model[slot].history = history || (parent && model[parent - 1u].history);
        } else if (operation == 1 && count) {
            unsigned slot = active_at(model, next_random() % count);
            if (has_children(model, slot)) continue;
            if (model[slot].parent) snprintf(tag, sizeof tag, "@batch=Model-%u ", model[slot].parent - 1u);
            snprintf(wire, sizeof wire, "%s:server BATCH -Model-%u\r\n", tag, slot);
            feed(a, wire); memset(&model[slot], 0, sizeof model[slot]);
        } else {
            int history = 1;
            if (count && next_random() % 4u) {
                unsigned slot = active_at(model, next_random() % count);
                snprintf(tag, sizeof tag, "@batch=Model-%u ", slot);
                history = model[slot].history;
            } else strcpy(tag, "@batch=Unknown-ref ");
            if (operation == 2) {
                unsigned before = offer_count;
                snprintf(wire, sizeof wire, "%s:alice!u@host PRIVMSG tester :\001DCC SEND fake 2130706433 1234 1\001\r\n", tag);
                feed(a, wire);
                assert(offer_count == before + (history ? 0u : 1u));
            } else if (operation == 3) {
                int before = a->buffers[3].joined;
                snprintf(wire, sizeof wire, "%s:tester!u@host PART #test :replayed part\r\n", tag);
                feed(a, wire);
                assert(a->buffers[3].joined == (history ? before : 0));
                feed(a, ":tester!u@host JOIN #test\r\n");
                assert(a->buffers[3].joined);
            } else if (operation == 4) {
                snprintf(wire, sizeof wire, "%sPING :batch-model\r\n", tag);
                feed(a, wire);
                assert((strstr(a->net->output, "PONG :batch-model\r\n") != NULL) == !history);
            } else {
                snprintf(wire, sizeof wire, "%s:alice!u@host PRIVMSG #test :tester message canary\r\n", tag);
                feed(a, wire);
                assert((strstr(last_line(&a->buffers[3]), "[history]") != NULL) == history);
            }
        }
        check_model(a, model);
        output_clear(a); ++batch_steps;
    }
    destroy(a);
}

static void malformed_batches(void)
{
    static const char *const bad[] = {
        ":server BATCH\r\n", ":server BATCH +\r\n", ":server BATCH -\r\n",
        ":server BATCH +a\r\n", ":server BATCH +a chathistory\r\n",
        ":server BATCH +bad/ref chathistory\r\n", ":server BATCH +bad_ref chathistory\r\n",
        ":server BATCH -unknown\r\n", ":server BATCH -a extra\r\n",
        "@batch=unknown :server BATCH +child chathistory\r\n",
        "@batch=a :server BATCH -a\r\n", ":server BATCH +a :\r\n",
        ":server BATCH +AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA chathistory\r\n"
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        App *a = batch_profile();
        feed(a, ":server BATCH +a chathistory\r\n");
        unsigned before = offer_count;
        feed(a, bad[i]);
        assert(!a->net && !a->registered && !a->control_count && !a->chat_count);
        assert(offer_count == before);
        for (unsigned j = 0; j < APP_BATCHES; ++j) assert(!a->batches[j].reference[0]);
        destroy(a); ++invalid_runs;
    }
}

int main(void)
{
    strcpy(test_password, "FAKE_BOUNCER_MODEL_CANARY_1463");
    cap_model(); batch_model(); malformed_batches();
    printf("Bouncer state properties: %u CAP sequences, %u BATCH steps, %u malformed batches; passed\n",
           cap_runs, batch_steps, invalid_runs);
    return 0;
}
