/* DCC action labels use terminal cells, not UTF-8 byte counts.
 * Mouse hits are calculated from the same cell widths as the drawn buttons.
 * This module is deliberately curses-free for ten-locale geometry tests. */
#ifndef VIPIRC_TRANSFER_UI_L10N_H
#define VIPIRC_TRANSFER_UI_L10N_H
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
#include "display_l10n.h"

static int irc_transfer_label_cells(const char *s)
{
    int cells = 0;
    mbstate_t state;
    memset(&state, 0, sizeof state);
    for (; *s && cells < 512;) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, s, MB_CUR_MAX, &state);
        if (n == (size_t)-1 || n == (size_t)-2 || n == 0) {
            ++s; ++cells;
            memset(&state, 0, sizeof state);
        } else {
            int width = wcwidth(wc);
            cells += width < 0 ? 1 : width;
            s += n;
        }
    }
    return cells;
}

/* Returns whether only the selected action fits. Non-visible actions have
 * position -1, so mouse presses cannot accidentally trigger hidden actions. */
static int irc_transfer_action_layout(const char *const labels[], size_t count,
                                      int cols, size_t selected,
                                      int positions[5], int widths[5])
{
    if (!labels || !positions || !widths || count == 0 || count > 5 ||
        selected >= count || cols < 8) return 1;
    int total = 0;
    for (size_t i = 0; i < 5; ++i) { positions[i] = -1; widths[i] = 0; }
    for (size_t i = 0; i < count; ++i) {
        widths[i] = irc_transfer_label_cells(labels[i]) + 2;
        total += widths[i] + 1;
    }
    if (total > cols) {
        widths[selected] = cols - 4;
        positions[selected] = 2;
        return 1;
    }
    int x = 0;
    for (size_t i = 0; i < count; ++i) {
        positions[i] = x;
        x += widths[i] + 1;
    }
    return 0;
}

static void transfer_editor_position(char *out, size_t cap, size_t bytes, int row, int col)
{
    char num_bytes[32], num_row[32], num_col[32];
    snprintf(num_bytes, sizeof num_bytes, "%zu", bytes);
    snprintf(num_row, sizeof num_row, "%d", row);
    snprintf(num_col, sizeof num_col, "%d", col);
    const IrcDisplayField fields[] = {
        {"bytes", num_bytes}, {"row", num_row}, {"col", num_col}};
    if (!irc_display_render(out, cap,
            "{bytes} UTF-8 bytes | row {row} col {col}", fields, 3))
        snprintf(out, cap, "%zu UTF-8 bytes | row %d col %d", bytes, row, col);
}
#endif
