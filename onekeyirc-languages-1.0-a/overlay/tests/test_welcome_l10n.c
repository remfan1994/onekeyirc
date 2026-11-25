#define IRC_L10N_IMPLEMENTATION
#include "../src/welcome_l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    const char *warning = "Not vegetarian? www.bloodguiltcurse.net will change your mind";
    const char *prompt = "Press a key to start.";
    assert(!strcmp(irc_welcome_warning(0), warning));
    assert(!strcmp(irc_welcome_prompt(0), prompt));
    assert(!strcmp(irc_welcome_warning(1), irc_tr(warning)));
    assert(!strcmp(irc_welcome_prompt(1), irc_tr(prompt)));
    assert(strstr(irc_welcome_warning(1), "www.bloodguiltcurse.net"));
    int localized = getenv("TEST_EXPECT_TRANSLATION") && *getenv("TEST_EXPECT_TRANSLATION");
    assert((strcmp(irc_welcome_warning(1), warning) != 0) == localized);
    assert((strcmp(irc_welcome_prompt(1), prompt) != 0) == localized);
    puts("Localized welcome, URL and ASCII fallback: PASS");
    return 0;
}
