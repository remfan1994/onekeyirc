/* Real compiled catalogs, conventional and extended numeric shapes. */
#define IRC_L10N_IMPLEMENTATION
#include "../src/uncommon_numeric_l10n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void must(int pass, int code, const char *why) {
    if (!pass) { fprintf(stderr, "numeric %d: %s\n", code, why); exit(1); }
}
int main(void) {
    char out[2048], tiny[2], expected[2048];
    char *params[] = {"RECIPIENT-ONLY", "VALUE1%n{nick}", "VALUE2%s{raw}", "VALUE3%p{detail}", "EXTRA-FIELD"};
    const int localized = getenv("TEST_LOCALIZED") != NULL;
    size_t checked = 0;
    for (size_t i = 0; i < sizeof irc_uncommon_numerics / sizeof irc_uncommon_numerics[0]; ++i) {
        const IrcUncommonNumeric *e = &irc_uncommon_numerics[i];
        IrcDisplayField f[3];
        for (size_t j = 0; j < e->count; ++j) {
            f[j].name = e->names[j]; f[j].value = params[j+1];
        }
        must((strcmp(irc_tr(e->english), e->english) != 0) == localized, e->code, "catalog or English fallback");
        must(irc_display_fields_valid(irc_tr(e->english), f, e->count), e->code, "field parity");
        must(irc_display_render(expected, sizeof expected, e->english, f, e->count), e->code, "expected rendering");
        must(irc_uncommon_numeric_render(out, sizeof out, e->code, params, e->count+1), e->code, "normal reply");
        must(!strcmp(out, expected), e->code, "wrong template");
        for (size_t j=1; j<=e->count; ++j) must(strstr(out, params[j])!=NULL, e->code, "literal field lost");
        must(!strstr(out, params[0]), e->code, "recipient leaked");
        must(!irc_uncommon_numeric_render(tiny, sizeof tiny, e->code, params, e->count+1) && !tiny[0], e->code, "overflow fallback");
        must(!irc_uncommon_numeric_render(out, sizeof out, e->code, params, e->count) && !out[0], e->code, "short reply fallback");
        must(!irc_uncommon_numeric_render(out, sizeof out, e->code, params, e->count+2) && !out[0], e->code, "extended reply fallback");
        checked++;
    }
    must(!irc_uncommon_numeric_render(out, sizeof out, 999, params, 3) && !out[0], 999, "unknown reply fallback");
    must(!irc_uncommon_numeric_render(out, sizeof out, 441, NULL, 4), 441, "null params");
    char *missing[] = {"recipient", "nick", NULL, "reason"};
    must(!irc_uncommon_numeric_render(out, sizeof out, 441, missing, 4), 441, "missing field");
    printf("Uncommon numeric replies: %zu templates PASS\n", checked);
    return 0;
}
