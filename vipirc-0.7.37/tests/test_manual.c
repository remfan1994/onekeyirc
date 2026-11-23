/* Exercise the actual embedded manual against the production Help buffer.
 * Reuse the in-memory transport fixture, not its two-line placeholder manual. */
#define vipirc_manual fixture_placeholder_manual
#define main fixture_app_test_main
#include "test_app.c"
#undef main
#undef vipirc_manual

extern const char *const vipirc_manual[];

int main(void)
{
    size_t expected = 0;
    while (vipirc_manual[expected]) ++expected;
    if (!expected || expected > APP_HELP_LINES) {
        fprintf(stderr, "manual has %zu lines; Help retains at least %u\n",
                expected, (unsigned)APP_HELP_LINES);
        return 1;
    }
    App *a = fresh(0); /* Fixture chooses the minimum 50-line history setting. */
    for (unsigned pass = 0; pass < 2; ++pass) {
        app_help(a);
        const Buffer *help = &a->buffers[2];
        assert(help->kind == B_HELP && help->line_count == expected);
        for (size_t i = 0; i < expected; ++i)
            assert(!strcmp(help->lines[(help->line_start + i) % help->line_cap],
                           vipirc_manual[i]));
        assert(!help->unread && help->scroll == SIZE_MAX);
    }
    destroy(a);
    printf("manual: all %zu lines retained, including first/last, at minimum history\n",
           expected);
    return 0;
}
