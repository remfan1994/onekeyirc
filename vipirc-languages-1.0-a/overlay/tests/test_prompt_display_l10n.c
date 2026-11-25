#define _XOPEN_SOURCE 700
#define IRC_L10N_IMPLEMENTATION
#include "../src/prompt_display_l10n.h"
#include <assert.h>
#include <locale.h>

int main(void)
{
    assert(setlocale(LC_CTYPE, "C.UTF-8") || setlocale(LC_CTYPE, "en_US.UTF-8"));
    const int english = !strcmp(getenv("VIPIRC_LANG"), "en");
    const char *keys[] = {
        "Run", "On: {target}", "Commands {first}-{last} of {total}", "Commands {total}; resize",
        "Enter: insert | ?: help | Delete: forget", "Down: suggestions | Up: history | Right: complete",
        "Down: suggestions", "Paste contained rejected characters or exceeded the command limit; review before running.",
        "Command paste captured. Review before running.",
        "Direct IRC staged. Previous bouncer identity, credentials and startup commands cleared; enter this server's authentication if required. The port is unchanged.",
        "Choice staged. Previous credentials and startup commands cleared; enter the bouncer identity and its password source. The provider's port is unchanged."
    };
    for (size_t i=0; i<sizeof keys/sizeof keys[0]; ++i)
        assert((strcmp(irc_tr(keys[i]), keys[i]) == 0) == english);
    char buf[512], tiny[2];
    assert(irc_prompt_target(buf, sizeof buf, "#nick{%s}"));
    assert(strstr(buf, "#nick{%s}"));
    assert(!irc_prompt_target(tiny, sizeof tiny, "#nick{%s}") && tiny[0] == 0);
    assert(irc_prompt_range(buf, sizeof buf, 12, 8, 57));
    assert(strstr(buf, "13") && strstr(buf, "20") && strstr(buf, "57"));
    assert(irc_prompt_range(buf, sizeof buf, 0, 0, 57) && strstr(buf,"57"));
    assert(irc_prompt_range(buf, sizeof buf, 0, 0, 0));
    assert(!strcmp(buf, irc_tr("Commands (0 matches)")));
    assert(irc_prompt_bytes(buf, sizeof buf, 483) && strstr(buf,"483"));
    char budget[512];
    assert(irc_prompt_budget(budget, sizeof budget, 510) && strstr(budget,"510"));
    int width=irc_prompt_cells(buf, INT_MAX)+3+irc_prompt_cells(budget, INT_MAX);
    assert(!irc_prompt_append(buf, sizeof buf, width-1, budget));
    assert(irc_prompt_append(buf, sizeof buf, width, budget));
    assert(irc_prompt_cells(buf, INT_MAX)==width && strstr(buf,"510"));
    assert(!irc_prompt_append(buf, sizeof buf, width, "X"));
    strcpy(buf,"a"); assert(!irc_prompt_append(buf, 4, 100, "x") && !strcmp(buf,"a"));
    assert(irc_prompt_cells("e\314\201", INT_MAX)==1);
    assert(irc_prompt_cells("\344\270\255", INT_MAX)==2);
    assert(irc_prompt_cells("\330\250\330\250", INT_MAX)==2);
    const char *full=keys[5], *compact=keys[6];
    int f=irc_prompt_cells(irc_tr(full), INT_MAX), c=irc_prompt_cells(irc_tr(compact), INT_MAX);
    assert(f>c && c>0);
    assert(irc_ui_hint(full,compact,f,irc_prompt_cells)==irc_tr(full));
    assert(irc_ui_hint(full,compact,c,irc_prompt_cells)==irc_tr(compact));
    assert(!irc_ui_hint(full,compact,c-1,irc_prompt_cells));
    unsigned checks=0;
    for (int columns=1; columns<=180; ++columns) {
        IrcPromptButtons b=irc_prompt_buttons(columns);
        if (columns<5) assert(!b.run_width && !b.cancel_width);
        else {
            assert(b.run_width>=2 && b.cancel_width>=2);
            assert(b.cancel_left==b.run_width+1 && b.cancel_left+b.cancel_width<=columns);
            int run=irc_prompt_cells(irc_tr("Run"), INT_MAX)+2;
            int cancel=irc_prompt_cells(irc_tr("Cancel"), INT_MAX)+2;
            if (run+cancel+1<=columns) assert(b.run_width==run && b.cancel_width==cancel);
        }
        for (int x=-1; x<=columns; ++x) {
            IrcPromptHit expected=IRC_PROMPT_NO_HIT;
            if (x>=0 && x<b.run_width) expected=IRC_PROMPT_RUN_HIT;
            else if (x>=b.cancel_left && x<b.cancel_left+b.cancel_width) expected=IRC_PROMPT_CANCEL_HIT;
            assert(irc_prompt_button_hit(b,x)==expected); ++checks;
        }
    }
    printf("Command entry formatting, hints and button hit testing: PASS (%u positions)\n",checks);
}
