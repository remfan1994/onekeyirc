/* Parse the bounded automatic-command allowlist into owned fields.
 * This module validates syntax only; App later enforces registration, verified
 * TLS, pacing and cancellation before executing it. No shell or UI is called.
 */
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>

/* Split the private scratch copy in place. Returned text borrows that
 * copy; only ConfigAutoCommand fields survive the parser's final wipe. */
static char *word(char **cursor)
{
    char *start = *cursor, *end;
    while (*start == ' ') ++start;
    end = start;
    while (*end && *end != ' ') ++end;
    if (*end) *end++ = 0;
    while (*end == ' ') ++end;
    *cursor = end;
    return start;
}

static int key_valid(const char *key)
{
    for (; *key; ++key) {
        unsigned char c = (unsigned char)*key;
        if (c <= 32 || c >= 127 || c == ':' || c == ',') return 0;
    }
    return 1;
}

/* 1=parsed, -1=invalid. out may be NULL for validation alone; when given,
 * it is cleared before parsing and receives a complete value only on success.
 * Message bodies and JOIN keys may be sensitive, including rejected input. */
int config_autocommand_parse(const char *text, ConfigAutoCommand *out, char *error, size_t cap)
{
    char copy[CONFIG_AUTOCOMMAND_SIZE] = {0};
    ConfigAutoCommand parsed;
    const char *problem = "Automatic command must be a single line of at most 511 UTF-8 bytes beginning with '/'.";
    int result = -1;
    memset(&parsed, 0, sizeof parsed);
    if (out) memset(out, 0, sizeof *out);
    if (!text || !*text || strnlen(text, sizeof copy) >= sizeof copy || text[0] != '/' ||
        !text[1] || text[1] == ' ' || !irc_utf8_valid(text)) goto done;
    for (size_t i = 0; text[i]; ++i)
        if ((unsigned char)text[i] < 32 || (unsigned char)text[i] == 127) goto done;
    memcpy(copy, text + 1, strlen(text));
    char *rest = copy, *verb = word(&rest), *target;
    for (size_t i = 0; verb[i]; ++i)
        if (verb[i] >= 'A' && verb[i] <= 'Z') verb[i] = (char)(verb[i] + ('a' - 'A'));
    if (!strcmp(verb, "msg") || !strcmp(verb, "message") || !strcmp(verb, "privmsg")) {
        problem = "Automatic /msg requires one nickname or channel and nonempty text.";
        target = word(&rest);
        if (!irc_valid_target(target) || !*rest) goto done;
        parsed.kind = CONFIG_AUTO_MSG;
        (void)snprintf(parsed.target, sizeof parsed.target, "%s", target);
        (void)snprintf(parsed.text, sizeof parsed.text, "%s", rest);
    } else if (!strcmp(verb, "join") || !strcmp(verb, "j")) {
        problem = "Automatic /join requires one channel and at most one printable ASCII key.";
        target = word(&rest);
        char *key = word(&rest);
        if (!irc_valid_channel(target) || *rest || !key_valid(key)) goto done;
        parsed.kind = CONFIG_AUTO_JOIN;
        (void)snprintf(parsed.target, sizeof parsed.target, "%s", target);
        (void)snprintf(parsed.text, sizeof parsed.text, "%s", key);
    } else if (!strcmp(verb, "nick") || !strcmp(verb, "nickname") || !strcmp(verb, "whois")) {
        problem = "Automatic /nick and /whois require exactly one nickname.";
        target = word(&rest);
        if (!irc_valid_nick(target) || *rest) goto done;
        parsed.kind = !strcmp(verb, "whois") ? CONFIG_AUTO_WHOIS : CONFIG_AUTO_NICK;
        (void)snprintf(parsed.target, sizeof parsed.target, "%s", target);
    } else if (!strcmp(verb, "away")) {
        parsed.kind = CONFIG_AUTO_AWAY;
        (void)snprintf(parsed.text, sizeof parsed.text, "%s", rest);
    } else if (!strcmp(verb, "back") || !strcmp(verb, "unaway")) {
        problem = "Automatic /back takes no arguments.";
        if (*rest) goto done;
        parsed.kind = CONFIG_AUTO_BACK;
    } else {
        problem = "Automatic commands support /msg, /join, /nick, /away, /back and /whois only.";
        goto done;
    }
    if (out) *out = parsed;
    result = 1;
done:
    if (error && cap) (void)snprintf(error, cap, "%s", result > 0 ? "" : problem);
    config_wipe(copy, sizeof copy);
    config_wipe(&parsed, sizeof parsed);
    return result;
}
