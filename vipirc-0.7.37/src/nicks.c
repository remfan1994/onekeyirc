/* Nickname completion is local editing, never a network command.
 * The completion object retains the original prefix while candidates cycle.
 * Candidates are read from the current roster each time; edits/view changes
 * invalidate the old range through the caller's reset and the checks below. */
#include "nicks.h"
#include <string.h>

static unsigned char folded(unsigned char c, IrcCase mapping)
{
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c + ('a' - 'A'));
    if (mapping != IRC_CASE_ASCII) {
        if (c == '[') return '{';
        if (c == ']') return '}';
        if (c == '\\') return '|';
        if (c == '^' && mapping == IRC_CASE_RFC1459) return '~';
    }
    return c;
}

static int compare(const char *a, const char *b, IrcCase mapping)
{
    while (*a && *b) {
        int d = (int)folded((unsigned char)*a++, mapping) -
                (int)folded((unsigned char)*b++, mapping);
        if (d) return d;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int prefix_match(const char *nick, const char *prefix, IrcCase mapping)
{
    while (*prefix) {
        if (!*nick || folded((unsigned char)*nick++, mapping) !=
                      folded((unsigned char)*prefix++, mapping)) return 0;
    }
    return 1;
}

static int nick_character(wchar_t c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           (c >= L'0' && c <= L'9') || c == L'-' || c == L'[' || c == L']' ||
           c == L'\\' || c == L'`' || c == L'_' || c == L'^' || c == L'{' ||
           c == L'|' || c == L'}' || c == L'~';
}

void nick_completion_reset(NickCompletion *c)
{
    memset(c, 0, sizeof *c);
}

static void candidate(const App *a, const NickCompletion *c, const char *nick,
                      int direction, char best[64], char wrap[64])
{
    if (!irc_valid_nick(nick) || !prefix_match(nick, c->prefix, a->casemap)) return;
    int d = compare(nick, c->previous, a->casemap);
    if ((!*wrap || (direction > 0 ? compare(nick, wrap, a->casemap) < 0 :
                                    compare(nick, wrap, a->casemap) > 0)))
        memcpy(wrap, nick, strlen(nick) + 1);
    if ((!c->active || (direction > 0 ? d > 0 : d < 0)) &&
        (!*best || (direction > 0 ? compare(nick, best, a->casemap) < 0 :
                                   compare(nick, best, a->casemap) > 0)))
        memcpy(best, nick, strlen(nick) + 1);
}

int nick_complete(const App *a, Editor *e, NickCompletion *c, int direction)
{
    char best[64] = "", wrap[64] = "", replacement[67];
    if (!a || !e || !c || e->pos > e->len || a->current >= a->buffer_count) return 0;
    if (!direction) direction = 1;
    if (c->active && (c->view != a->current || e->pos != c->end || c->end > e->len))
        nick_completion_reset(c);
    if (!c->active) {
        size_t first = e->pos, end = e->pos;
        while (first && nick_character(e->data[first - 1])) --first;
        while (end < e->len && nick_character(e->data[end])) ++end;
        /* Do not replace only the ASCII portion of a Unicode word or leave
         * the caret inside a base/combining-mark group. IRC nicks here are ASCII. */
        if (end < e->len && (unsigned long)e->data[end] >= 128ul) return 0;
        /* A nickname must be its own token, not part of #channel or a URL. */
        if (first && e->data[first - 1] != L' ' && e->data[first - 1] != L'\n' &&
            e->data[first - 1] != L'(' && e->data[first - 1] != L'<') return 0;
        size_t length = e->pos - first;
        if (length >= sizeof c->prefix || end - first >= sizeof c->prefix) return 0;
        nick_completion_reset(c);
        for (size_t i = 0; i < length; ++i) c->prefix[i] = (char)e->data[first + i];
        c->first = first;
        c->end = end;
        c->view = a->current;
        if (!first && (end == e->len || e->data[end] != L':'))
            strcpy(c->suffix, end == e->len ? ": " : ":");
        else if (end == e->len) strcpy(c->suffix, " ");
    }
    const Buffer *b = &a->buffers[a->current];
    if (b->kind == B_CHANNEL) {
        for (size_t i = 0; i < b->user_count; ++i)
            candidate(a, c, b->users[i].nick, direction, best, wrap);
    } else if (b->kind == B_QUERY) {
        candidate(a, c, b->name, direction, best, wrap);
        candidate(a, c, a->nick, direction, best, wrap);
    } else {
        for (size_t i = 3; i < a->buffer_count; ++i) {
            const Buffer *view = &a->buffers[i];
            if (view->kind == B_QUERY) candidate(a, c, view->name, direction, best, wrap);
            else if (view->kind == B_CHANNEL && view->joined)
                for (size_t j = 0; j < view->user_count; ++j)
                    candidate(a, c, view->users[j].nick, direction, best, wrap);
        }
    }
    if (!*best) memcpy(best, wrap, sizeof best);
    if (!*best) {
        nick_completion_reset(c);
        return 0;
    }
    size_t nick_length = strlen(best), suffix_length = strlen(c->suffix);
    memcpy(replacement, best, nick_length);
    memcpy(replacement + nick_length, c->suffix, suffix_length + 1);
    size_t length = nick_length + suffix_length, removed = c->end - c->first;
    /* Both replaced and inserted scalars are ASCII; byte accounting is exact.
     * Validate the whole change before touching a full or nearly-full draft. */
    if (removed > e->bytes || e->len - removed > ED_MAX_CHARS - length ||
        e->bytes - removed > ED_MAX_BYTES - length) return -1;
    memmove(e->data + c->first + length, e->data + c->end,
            (e->len - c->end + 1) * sizeof e->data[0]);
    for (size_t i = 0; i < length; ++i) e->data[c->first + i] = (wchar_t)(unsigned char)replacement[i];
    e->len = e->len - removed + length;
    e->bytes = e->bytes - removed + length;
    e->pos = c->end = c->first + length;
    e->goal_col = -1;
    e->selection_all = 0;
    memcpy(c->previous, best, sizeof c->previous);
    c->active = 1;
    return 1;
}
