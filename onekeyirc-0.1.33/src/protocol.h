#ifndef VIPIRC_PROTOCOL_H
#define VIPIRC_PROTOCOL_H

#include <stddef.h>

/* Application bounds, not claims about every IRC network's policy. */
#define IRC_WIRE_MAX 16384u
#define IRC_BODY_MAX 510u
#define IRC_NICK_MAX 63u
#define IRC_TARGET_MAX 200u
#define IRC_IGNORE_MASK_MAX 127u

typedef enum IrcCase {
    IRC_CASE_RFC1459,
    IRC_CASE_STRICT,
    IRC_CASE_ASCII
} IrcCase;

typedef struct IrcMessage {
    char *tags;
    char *prefix;
    char *command;
    char *params[15];
    size_t nparams;
} IrcMessage;

typedef struct IrcChunk {
    size_t offset;
    size_t length;
} IrcChunk;

/* Parse a writable, NUL-terminated, CRLF-stripped line in place. Command
 * letters are uppercased. Returned pointers refer to line. Before calling,
 * the transport must reject embedded NUL bytes. Maximum ordinary body is
 * 510 bytes, with up to 8191 additional bytes for tags. Failure clears *out.
 * Every field is borrowed: consume or copy it before reusing line. Successful
 * parsing validates syntax, not identity, authorization or terminal safety. */
int irc_parse(char *line, IrcMessage *out);
/* Decode the final occurrence of a case-sensitive IRCv3 tag. 1=present,
 * 0=absent, -1=value does not fit. Empty/missing values are equivalent.
 * No allocation; escaped controls are data and must not reach a terminal. */
int irc_tag_value(const char *tags, const char *key, char *out, size_t cap);
/* Validate a UTC server-time tag, including calendar bounds/leap seconds,
 * and render its date/time to whole seconds with an explicit UTC suffix.
 * Return 1 on success; invalid or absent timestamps return 0 with empty out. */
int irc_server_time(const char *tags, char *out, size_t cap);
int irc_equal(const char *a, const char *b, IrcCase mapping);
int irc_valid_nick(const char *nick);
int irc_valid_channel(const char *channel);
/* Accept one nick or one channel; lists, masks, and host-directed targets
 * deliberately are not supported by this client. */
int irc_valid_target(const char *target);
/* Exact nickname, or three nonempty ASCII nick!user@host components.
 * Full masks support only '*' (zero or more bytes) and '?' (one byte);
 * brackets, backslashes and other permitted punctuation are literal.
 * Maximum mask length is 127 bytes. Wildcards never cross delimiters. */
int irc_valid_ignore_mask(const char *mask);
/* Nick matching uses the server's CASEMAPPING; user/host matching folds
 * ASCII case only. A nick-only mask needs no user/host. Full masks require
 * bounded source components: nick<=63, user<=63, host<=255 bytes.
 * This does not establish that a prefix is a user: the caller must do so.
 * Fixed memory, no allocation/recursion, O(mask bytes * source bytes). */
int irc_ignore_match(const char *mask, const char *nick, const char *user,
                     const char *host, IrcCase mapping);
int irc_utf8_valid(const char *text);
/* Store only complete UTF-8 characters. Invalid input bytes and unsafe
 * controls become '?'; tabs and line separators become spaces. IRC styling
 * and color parameters are removed. In-place sanitizing is allowed. Return
 * bytes stored, excluding the terminating NUL. Never execute this output. */
size_t irc_sanitize(char *dst, size_t cap, const char *src);
/* Nick matching observes IRC nickname boundaries. Custom literal matching
 * folds ASCII/IRC symbols only; Unicode characters match byte-exactly. */
int irc_mentions(const char *text, const char *nick, IrcCase mapping);
int irc_contains(const char *text, const char *needle, IrcCase mapping);
/* Maximum text bytes after accounting for a complete relayed message.
 * wire_limit==0 means 512; larger limits are conservatively capped at 512.
 * Missing identity reserves nick=63, user=32, host=255 bytes. */
size_t irc_payload_budget(size_t wire_limit, const char *command,
                          const char *target, const char *nick,
                          const char *user, const char *host);
/* Split at UTF-8 character boundaries, preserving every input byte.
 * CR/LF input is rejected. Returns 1 on success (empty input -> count 0),
 * 0 for invalid input, too-small budget, or insufficient chunk capacity.
 * On failure *count is zero and the chunk array must not be used. */
int irc_split(const char *text, size_t budget, IrcChunk *chunks,
              size_t cap, size_t *count);

#endif
