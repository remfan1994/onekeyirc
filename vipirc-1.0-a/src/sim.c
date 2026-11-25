/* Bounded in-process IRC peer for --test-mode, not a network server.
 * emit queues reply bytes; command handles parsed client input; sim_step
 * schedules demonstration events. App reads those bytes through its normal
 * framing/dispatch path. No network, credentials or application callbacks.
 */
#define _POSIX_C_SOURCE 200809L
#include "sim.h"
#include "protocol.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SIM_QUEUE_BYTES 65536u
#define SIM_CHANNELS 29u
#define SIM_PERIOD_MS 2000

typedef struct SimChannel {
    char name[201], topic[256];
    int joined;
} SimChannel;
struct Sim {
    unsigned char bytes[SIM_QUEUE_BYTES];
    size_t head, used, dropped;
    char incoming[IRC_BODY_MAX + 2];
    size_t incoming_len;
    SimChannel channels[SIM_CHANNELS];
    char nick[64], peers[3][64], guest[64];
    int have_user, cap_done, registered, failed, away;
    size_t event, round_robin, guest_channel, watch_cycle;
    int guest_active, guest_voice;
    int64_t next_event;
    char error[160];
};

/* This offline simulator uses the same monotonic
 * timeline as the live pollers; fixture timeouts are not wall-clock
 * calendar timestamps or claims about real server performance. */
int64_t sim_now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void copy_text(char *out, size_t cap, const char *text)
{
    size_t n = strlen(text);
    if (n >= cap) n = cap - 1;
    memcpy(out, text, n);
    out[n] = 0;
}

/* The reply ring drops whole frames at capacity. Never retain a partial
 * synthetic frame that could splice into the next reply. */
static int emit(Sim *s, const char *format, ...)
{
    char line[IRC_BODY_MAX + 3];
    va_list ap;
    int length;
    size_t n, tail, first;
    va_start(ap, format);
    length = vsnprintf(line, IRC_BODY_MAX + 1, format, ap);
    va_end(ap);
    if (length < 1 || (size_t)length > IRC_BODY_MAX) {
        if (s->dropped < SIZE_MAX) ++s->dropped;
        return 0;
    }
    for (n = 0; n < (size_t)length; ++n)
        if (line[n] == '\r' || line[n] == '\n') return 0;
    line[length] = '\r';
    line[length + 1] = '\n';
    n = (size_t)length + 2;
    if (n > SIM_QUEUE_BYTES - s->used) {
        if (s->dropped < SIZE_MAX) ++s->dropped;
        return 0;
    }
    tail = (s->head + s->used) % SIM_QUEUE_BYTES;
    first = SIM_QUEUE_BYTES - tail;
    if (first > n) first = n;
    memcpy(s->bytes + tail, line, first);
    if (n > first) memcpy(s->bytes, line + first, n - first);
    s->used += n;
    return 1;
}

static void unique_peer(Sim *s, char *out, const char *base)
{
    copy_text(out, 64, base);
    if (irc_equal(out, s->nick, IRC_CASE_RFC1459)) {
        size_t n = strlen(out);
        out[n] = '_';
        out[n + 1] = 0;
    }
}

static void choose_peers(Sim *s)
{
    unique_peer(s, s->peers[0], "Aster");
    unique_peer(s, s->peers[1], "Birch");
    unique_peer(s, s->peers[2], "Cedar");
}

static int occupied_nick(const Sim *s, const char *nick)
{
    size_t i;
    for (i = 0; i < 3; ++i)
        if (irc_equal(nick, s->peers[i], IRC_CASE_RFC1459)) return 1;
    return s->guest_active && irc_equal(nick, s->guest, IRC_CASE_RFC1459);
}

static size_t find_channel(const Sim *s, const char *name)
{
    size_t i;
    for (i = 0; i < SIM_CHANNELS; ++i)
        if (s->channels[i].name[0] && irc_equal(s->channels[i].name, name, IRC_CASE_RFC1459))
            return i;
    return SIZE_MAX;
}

static size_t channel_slot(Sim *s, const char *name)
{
    size_t i = find_channel(s, name), spare = SIZE_MAX;
    if (i != SIZE_MAX) return i;
    for (i = 0; i < SIM_CHANNELS; ++i) {
        if (!s->channels[i].name[0]) {
            spare = i;
            break;
        }
        if (!s->channels[i].joined && spare == SIZE_MAX) spare = i;
    }
    if (spare != SIZE_MAX) {
        memset(&s->channels[spare], 0, sizeof(s->channels[spare]));
        copy_text(s->channels[spare].name, sizeof(s->channels[spare].name), name);
        copy_text(s->channels[spare].topic, sizeof(s->channels[spare].topic),
                  "Offline test channel. All activity is simulated.");
    }
    return spare;
}

static void names(Sim *s, size_t index)
{
    SimChannel *c = &s->channels[index];
    if (s->guest_active && s->guest_channel == index)
        (void)emit(s, ":sim.local 353 %s = %s :@%s +%s %s %s %s%s", s->nick, c->name,
                    s->peers[0], s->peers[1], s->peers[2], s->nick,
                    s->guest_voice ? "+" : "", s->guest);
    else
        (void)emit(s, ":sim.local 353 %s = %s :@%s +%s %s %s", s->nick, c->name,
                    s->peers[0], s->peers[1], s->peers[2], s->nick);
    (void)emit(s, ":sim.local 366 %s %s :End of simulated user list", s->nick, c->name);
}

static void maybe_register(Sim *s)
{
    if (s->registered || !s->cap_done || !s->have_user || !s->nick[0]) return;
    s->registered = 1;
    (void)emit(s, ":sim.local 001 %s :OFFLINE TEST MODE: in-process IRC simulation", s->nick);
    (void)emit(s, ":sim.local 005 %s CASEMAPPING=rfc1459 PREFIX=(ov)@+ CHANMODES=b,k,l,imnst NICKLEN=63 CHANNELLEN=200 TOPICLEN=255 :simulated settings", s->nick);
    (void)emit(s, ":sim.local 375 %s :- Offline simulator MOTD", s->nick);
    (void)emit(s, ":sim.local 372 %s :- No network, DNS, TLS or authentication is performed.", s->nick);
    (void)emit(s, ":sim.local 372 %s :- All users and messages here are simulated. Configuration writes are disabled.", s->nick);
    (void)emit(s, ":sim.local 372 %s :- Try /channels, /ql, mentions, long drafts, /join, /part and /close.", s->nick);
    (void)emit(s, ":sim.local 376 %s :End of offline simulator MOTD", s->nick);
}

/* Parsed command fields borrow sim_write's current incoming buffer. */
static void command(Sim *s, IrcMessage *m)
{
    const char *cmd = m->command;
    const char *me = s->nick[0] ? s->nick : "*";
    size_t i;
    if (!strcmp(cmd, "CAP") && m->nparams) {
        if (!strcmp(m->params[0], "LS"))
            (void)emit(s, ":sim.local CAP %s LS :multi-prefix", me);
        else if (!strcmp(m->params[0], "REQ") && m->nparams > 1) {
            if (!strcmp(m->params[1], "multi-prefix"))
                (void)emit(s, ":sim.local CAP %s ACK :multi-prefix", me);
            else (void)emit(s, ":sim.local CAP %s NAK :%s", me, m->params[1]);
        } else if (!strcmp(m->params[0], "END")) {
            s->cap_done = 1;
            maybe_register(s);
        }
        return;
    }
    if (!strcmp(cmd, "NICK") && m->nparams) {
        if (!irc_valid_nick(m->params[0])) {
            (void)emit(s, ":sim.local 432 %s %s :Invalid simulated nickname", me, m->params[0]);
            return;
        }
        if (s->registered && occupied_nick(s, m->params[0])) {
            (void)emit(s, ":sim.local 433 %s %s :That simulated nickname is already present", me, m->params[0]);
            return;
        }
        if (s->registered)
            (void)emit(s, ":%s!offline@sim.local NICK :%s", s->nick, m->params[0]);
        copy_text(s->nick, sizeof(s->nick), m->params[0]);
        if (!s->registered) choose_peers(s);
        maybe_register(s);
        return;
    }
    if (!strcmp(cmd, "USER")) {
        s->have_user = 1;
        maybe_register(s);
        return;
    }
    if (!strcmp(cmd, "PASS") || !strcmp(cmd, "AUTHENTICATE")) {
        (void)emit(s, "ERROR :Authentication commands are disabled in OFFLINE TEST MODE");
        return;
    }
    if (!strcmp(cmd, "PING") && m->nparams) {
        (void)emit(s, ":sim.local PONG sim.local :%s", m->params[m->nparams - 1]);
        return;
    }
    if (!strcmp(cmd, "PONG")) return;
    if (!strcmp(cmd, "QUIT")) {
        s->registered = 0;
        return;
    }
    if (!s->registered) {
        (void)emit(s, ":sim.local 451 %s :Complete simulated registration first", me);
        return;
    }
    if (!strcmp(cmd, "USERHOST")) {
        (void)emit(s, ":sim.local 302 %s :%s=%coffline@sim.local", s->nick, s->nick,
                   s->away ? '-' : '+');
    } else if (!strcmp(cmd, "AWAY")) {
        s->away = m->nparams && m->params[0][0];
        (void)emit(s, ":sim.local %s %s :%s", s->away ? "306" : "305", s->nick,
                   s->away ? "You have been marked as being away" : "You are no longer marked as being away");
    } else if (!strcmp(cmd, "JOIN") && m->nparams && irc_valid_channel(m->params[0])) {
        i = channel_slot(s, m->params[0]);
        if (i == SIZE_MAX) {
            (void)emit(s, ":sim.local 405 %s %s :Simulated channel limit is 29", s->nick, m->params[0]);
            return;
        }
        if (s->channels[i].joined) {
            (void)emit(s, ":sim.local 443 %s %s %s :Already in simulated channel", s->nick, s->nick, m->params[0]);
            return;
        }
        s->channels[i].joined = 1;
        (void)emit(s, ":%s!offline@sim.local JOIN :%s", s->nick, s->channels[i].name);
        (void)emit(s, ":sim.local 332 %s %s :%s", s->nick, s->channels[i].name, s->channels[i].topic);
        names(s, i);
    } else if (!strcmp(cmd, "PART") && m->nparams) {
        i = find_channel(s, m->params[0]);
        if (i == SIZE_MAX || !s->channels[i].joined) {
            (void)emit(s, ":sim.local 442 %s %s :Not in this simulated channel", s->nick, m->params[0]);
            return;
        }
        (void)emit(s, ":%s!offline@sim.local PART %s :Leaving simulated channel", s->nick, s->channels[i].name);
        s->channels[i].joined = 0;
        if (s->guest_active && s->guest_channel == i) s->guest_active = 0;
    } else if (!strcmp(cmd, "WHOIS") && m->nparams && irc_valid_nick(m->params[0])) {
        (void)emit(s, ":sim.local 311 %s %s offline sim.local * :Simulated participant", s->nick, m->params[0]);
        (void)emit(s, ":sim.local 312 %s %s sim.local :In-process offline simulator", s->nick, m->params[0]);
        (void)emit(s, ":sim.local 318 %s %s :End of simulated WHOIS", s->nick, m->params[0]);
    } else if (!strcmp(cmd, "TOPIC") && m->nparams) {
        i = find_channel(s, m->params[0]);
        if (i == SIZE_MAX || !s->channels[i].joined) {
            (void)emit(s, ":sim.local 442 %s %s :Not in this simulated channel", s->nick, m->params[0]);
            return;
        }
        if (m->nparams > 1) {
            size_t budget = irc_payload_budget(512, "TOPIC", s->channels[i].name,
                                               s->nick, "offline", "sim.local");
            if (strlen(m->params[1]) >= sizeof(s->channels[i].topic) ||
                strlen(m->params[1]) > budget) {
                (void)emit(s, ":sim.local 417 %s :Topic exceeds the simulated 255-byte or full-frame limit", s->nick);
                return;
            }
            irc_sanitize(s->channels[i].topic, sizeof(s->channels[i].topic), m->params[1]);
            (void)emit(s, ":%s!offline@sim.local TOPIC %s :%s", s->nick, s->channels[i].name, s->channels[i].topic);
        } else {
            (void)emit(s, ":sim.local 332 %s %s :%s", s->nick, s->channels[i].name, s->channels[i].topic);
        }
    } else if (!strcmp(cmd, "PRIVMSG") && m->nparams >= 2) {
        if (irc_valid_channel(m->params[0])) {
            i = find_channel(s, m->params[0]);
            if (i == SIZE_MAX || !s->channels[i].joined)
                (void)emit(s, ":sim.local 404 %s %s :Not in simulated channel", s->nick, m->params[0]);
        } else if (irc_valid_nick(m->params[0]) && !irc_equal(m->params[0], s->nick, IRC_CASE_RFC1459)) {
            (void)emit(s, ":%s!demo@sim.local PRIVMSG %s :Offline reply: this is a simulated query; no message left this process.", m->params[0], s->nick);
        }
    } else {
        (void)emit(s, ":sim.local 421 %s %s :Command is not simulated", s->nick, cmd);
    }
}

/* Public simulator lifetime and the Net-like byte-stream interface. */
Sim *sim_open(char *error, size_t error_cap)
{
    Sim *s = calloc(1, sizeof(*s));
    if (!s) {
        if (error && error_cap) (void)snprintf(error, error_cap, "Cannot allocate offline simulator");
        return NULL;
    }
    s->next_event = sim_now_ms() + SIM_PERIOD_MS;
    choose_peers(s);
    return s;
}

void sim_close(Sim *s)
{
    if (s) {
        volatile unsigned char *p = (volatile unsigned char *)s;
        size_t n = sizeof(*s);
        while (n--) *p++ = 0;
        free(s);
    }
}

ssize_t sim_read(Sim *s, void *out, size_t cap)
{
    size_t n, first;
    if (s->failed) return -1;
    if (!s->used || !cap) return -2;
    n = s->used < cap ? s->used : cap;
    first = SIM_QUEUE_BYTES - s->head;
    if (first > n) first = n;
    memcpy(out, s->bytes + s->head, first);
    if (n > first) memcpy((unsigned char *)out + first, s->bytes, n - first);
    s->head = (s->head + n) % SIM_QUEUE_BYTES;
    s->used -= n;
    return (ssize_t)n;
}

ssize_t sim_write(Sim *s, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    size_t i;
    if (s->failed) return -1;
    for (i = 0; i < size; ++i) {
        unsigned char c = bytes[i];
        if (!c) goto malformed;
        if (c == '\n') {
            IrcMessage m;
            if (s->incoming_len && s->incoming[s->incoming_len - 1] == '\r')
                --s->incoming_len;
            s->incoming[s->incoming_len] = 0;
            if (s->incoming_len && !irc_parse(s->incoming, &m))
                goto malformed;
            if (s->incoming_len)
                command(s, &m);
            /* The complete command has been consumed. Unprocessed bytes are
             * still in the caller's buffer; do not retain a JOIN key here. */
            volatile unsigned char *clear = (volatile unsigned char *)s->incoming;
            for (size_t j = 0; j < sizeof(s->incoming); ++j)
                clear[j] = 0;
            s->incoming_len = 0;
        } else {
            if (s->incoming_len == sizeof(s->incoming) - 1 ||
                (s->incoming_len && s->incoming[s->incoming_len - 1] == '\r')) goto malformed;
            s->incoming[s->incoming_len++] = (char)c;
        }
    }
    return (ssize_t)size;
malformed:
    copy_text(s->error, sizeof(s->error), "Malformed command rejected by offline simulator");
    s->failed = 1;
    return -1;
}

/* Timed demonstration traffic, round-robin across joined channels. */
static size_t next_joined(Sim *s)
{
    size_t n, i;
    for (n = 0; n < SIM_CHANNELS; ++n) {
        i = (s->round_robin + n) % SIM_CHANNELS;
        if (s->channels[i].joined) {
            s->round_robin = (i + 1) % SIM_CHANNELS;
            return i;
        }
    }
    return SIZE_MAX;
}

int sim_step(Sim *s, int64_t now, const char *watch_text)
{
    size_t channel;
    const char *name;
    char safe_watch[128];
    if (s->failed) return -1;
    if (!s->registered || now < s->next_event) return 1;
    /* Never catch up with a burst after suspend or a long inactive interval. */
    s->next_event = now + SIM_PERIOD_MS;
    if (s->dropped && SIM_QUEUE_BYTES - s->used >= 512) {
        size_t dropped = s->dropped;
        s->dropped = 0;
        (void)emit(s, ":sim.local NOTICE %s :Offline simulator discarded %zu events at its bounded frame/output limits.", s->nick, dropped);
        return 1;
    }
    channel = next_joined(s);
    name = channel == SIZE_MAX ? NULL : s->channels[channel].name;
    switch (s->event++ % 8) {
    case 0:
        if (name) (void)emit(s, ":%s!demo@sim.local PRIVMSG %s :Offline demo: a channel conversation is active.", s->peers[0], name);
        else (void)emit(s, ":sim.local NOTICE %s :OFFLINE TEST MODE is running. Use /join #name to preview a channel.", s->nick);
        break;
    case 1:
        if (name) (void)emit(s, ":%s!demo@sim.local PRIVMSG %s :%s: this is a simulated mention.", s->peers[1], name, s->nick);
        else (void)emit(s, ":%s!demo@sim.local PRIVMSG %s :%s: this is a simulated mention.", s->peers[1], s->nick, s->nick);
        break;
    case 2:
        ++s->watch_cycle;
        irc_sanitize(safe_watch, sizeof(safe_watch), watch_text ? watch_text : "");
        if (name && *safe_watch)
            (void)emit(s, ":%s!demo@sim.local PRIVMSG %s :Configured watch text: %s", s->peers[2], name, safe_watch);
        else if (name)
            (void)emit(s, ":%s!demo@sim.local PRIVMSG %s :Use /watch add ecology to preview a custom mention filter.", s->peers[2], name);
        break;
    case 3:
        (void)emit(s, ":%s!demo@sim.local PRIVMSG %s :This is a simulated private message. Open the query list with /ql.", s->peers[0], s->nick);
        break;
    case 4:
        if (name) {
            s->guest_channel = channel;
            s->guest_active = 1;
            s->guest_voice = 0;
            unique_peer(s, s->guest, "Sprout");
            (void)emit(s, ":%s!demo@sim.local JOIN :%s", s->guest, name);
        }
        break;
    case 5:
        if (s->guest_active && s->channels[s->guest_channel].joined) {
            char newnick[64];
            unique_peer(s, newnick, "Leaf");
            (void)emit(s, ":%s!demo@sim.local NICK :%s", s->guest, newnick);
            copy_text(s->guest, sizeof(s->guest), newnick);
        }
        break;
    case 6:
        if (s->guest_active && s->channels[s->guest_channel].joined) {
            s->guest_voice = 1;
            (void)emit(s, ":%s!demo@sim.local MODE %s +v %s", s->peers[0], s->channels[s->guest_channel].name, s->guest);
        }
        break;
    case 7:
        if (s->guest_active) {
            if (s->channels[s->guest_channel].joined)
                (void)emit(s, ":%s!demo@sim.local PART %s :End of simulated roster example", s->guest, s->channels[s->guest_channel].name);
            s->guest_active = 0;
        }
        break;
    }
    return 1;
}

size_t sim_watch_index(const Sim *s)
{
    return s ? s->watch_cycle : 0;
}

int sim_pending(const Sim *s)
{
    return s && s->used != 0;
}

int sim_failed(const Sim *s)
{
    return !s || s->failed;
}

const char *sim_error(const Sim *s)
{
    return s ? s->error : "Offline simulator is stopped";
}

int sim_timeout(const Sim *s, int64_t now)
{
    int64_t remaining;
    if (!s || !s->registered) return 1000;
    if (s->used) return 0;
    remaining = s->next_event - now;
    if (remaining <= 0) return 0;
    return remaining < 1000 ? (int)remaining : 1000;
}
