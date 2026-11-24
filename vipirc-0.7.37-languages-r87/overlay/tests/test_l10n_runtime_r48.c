/* r48: DCC button width/mouse geometry, localized states and literal data. */
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include "transfer_ui_l10n.h"
#include <assert.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    static const char *languages[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
    static const char *states[] = {"offered","waiting for peer","connecting","sending",
        "receiving","complete","rejected","cancelled","failed","empty"};
    const char *list[] = {"Accept","Reject","Send...","Cancel transfer","Back"};
    assert(setlocale(LC_CTYPE, "C.utf8") != NULL);
    /* UTF-8 bytes and terminal columns are different quantities. */
    assert(irc_transfer_label_cells("\xe4\xb8\xad") == 2);
    assert(irc_transfer_label_cells("a\xcc\x81") == 1);
    for (size_t lang = 0; lang < sizeof languages / sizeof languages[0]; ++lang) {
        assert(setenv("VIPIRC_LANG", languages[lang], 1) == 0);
        const char *labels[5];
        int positions[5], widths[5];
        int old_total = 0, cell_total = 0;
        for (size_t i = 0; i < 5; ++i) {
            labels[i] = irc_tr(list[i]);
            assert(strcmp(labels[i], list[i]) != 0 || strcmp(list[i], "Back") == 0);
            old_total += (int)strlen(labels[i]) + 3;
            cell_total += irc_transfer_label_cells(labels[i]) + 3;
        }
        int compact = irc_transfer_action_layout(labels, 5, 200, 0, positions, widths);
        assert(!compact);
        assert(cell_total <= old_total); /* Width never exceeds encoded byte count. */
        int previous = -1;
        for (size_t i = 0; i < 5; ++i) {
            assert(widths[i] == irc_transfer_label_cells(labels[i]) + 2);
            assert(positions[i] > previous);
            previous = positions[i] + widths[i];
            /* A click in the middle of a visible button selects exactly it. */
            int hit = positions[i] + widths[i] / 2;
            int matched = -1;
            for (size_t j = 0; j < 5; ++j)
                if (hit >= positions[j] && hit < positions[j] + widths[j]) matched = (int)j;
            assert(matched == (int)i);
        }
        for (size_t selected = 0; selected < 5; ++selected) {
            compact = irc_transfer_action_layout(labels, 5, 20, selected, positions, widths);
            assert(compact);
            for (size_t i = 0; i < 5; ++i) {
                if (i == selected) { assert(positions[i] == 2 && widths[i] == 16); }
                else assert(positions[i] == -1);
            }
        }
        const char *browser[] = {irc_tr("Choose"), irc_tr("Type path"), irc_tr("Cancel")};
        compact = irc_transfer_action_layout(browser, 3, 120, 1, positions, widths);
        assert(!compact && positions[0] == 0 && positions[1] > positions[0]);
        for (size_t state = 0; state < sizeof states / sizeof states[0]; ++state)
            assert(strcmp(irc_tr(states[state]), states[state]) != 0);

        char rendered[1000], payload[] = "name%p{not-a-field}\\/tmp/file";
        const IrcDisplayField f[] = {
            {"state", irc_tr("offered")}, {"nick", payload},
            {"transferred", "22"}, {"size", "93"}};
        assert(irc_display_render(rendered, sizeof rendered,
            "{state} with {nick}: {transferred}/{size} bytes.", f, 4));
        assert(strstr(rendered, payload) != NULL);
        assert(strstr(rendered, "22/93") != NULL);
        assert(!irc_display_render(rendered, 5,
            "{state} with {nick}: {transferred}/{size} bytes.", f, 4));
        assert(rendered[0] == 0);

        char footer[256];
        transfer_editor_position(footer, sizeof footer, 133, 7, 2);
        assert(strstr(footer, "133") && strstr(footer, "7") && strstr(footer, "2"));
    }
    assert(setenv("VIPIRC_LANG", "en", 1) == 0);
    assert(strcmp(irc_tr("Accept"), "Accept") == 0);
    puts("r48: ten-locale DCC layout, mouse ranges, bounded literals and English fallback PASS");
    return 0;
}
