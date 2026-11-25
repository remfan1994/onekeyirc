#define main existing_setup_main
#include "test_idle_setup.c"
#undef main
#include "l10n.h"
#include "commands.h"
int main(void) {
    const char *langs[] = {"en","id","ms","tr","es","pt","fr","de","ru","it","ar"};
    const char *values[] = {"", "250", "review", "fullscreen", "full", "no", "no", "yes", "yes", "contextual"};
    for (size_t i = 0; i < sizeof langs / sizeof *langs; ++i) {
        assert(!setenv("ONEKEYIRC_LANG", langs[i], 1));
        assert(!setenv("VIPIRC_LANG", langs[i], 1));
        Fixture f; init(&f, 0);
        IdleSetup *setup = begin(&f, 0);
        for (size_t j = 0; j < sizeof values / sizeof *values; ++j) answer(setup, values[j], 1);
        IdleSetupView cur = view(setup);
        assert(!strcmp(cur.question, irc_tr("Use signals")));
        assert(!strcmp(cur.current, "no"));
        if (i) assert(strcmp(cur.question, "Use signals"));
        answer(setup, "yes", 1);
        if (!view(setup).final) answer(setup, "idle", 1);
        cur = view(setup); assert(cur.final);
        assert(strstr(cur.summary, irc_tr("Use signals:")));
        assert(strstr(cur.summary, irc_tr(commands_suggestion_mode_label(CONFIG_SUGGEST_CONTEXTUAL))));
        assert(!strcmp(cur.current, "1"));
        idle_setup_free(setup); cleanup(&f);
        printf("%s actual guided setup question and review: PASS\n", langs[i]);
    }
    return 0;
}
