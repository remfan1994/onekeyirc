/* Program entry and session lifetime. See LICENSE.md.
 *
 * main() first separates informational/management invocations from startup,
 * then loads configuration and applies connection overrides to their selected
 * profile. The final driver owns Client, Background and DccManager across
 * frontend replacement. Frontends own presentation, not those shared objects.
 */
#include "app.h"
#include "client.h"
#include "ui.h"
#include "idle.h"
#include "idle_commands.h"
#include "l10n.h"
#include "security_diagnostic_l10n.h"
#include "manual.h"
#include "background.h"
#include "dcc.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#ifdef ONEKEY_ONLY
#define PROGRAM_NAME "onekeyirc"
#define PROGRAM_VERSION ONEKEY_VERSION
#define DEFAULT_IDLE 1
#else
#define PROGRAM_NAME "vipirc"
#define PROGRAM_VERSION VIPIRC_VERSION
#define DEFAULT_IDLE 0
#endif

static int reserve_standard_descriptors(void)
{
    /* Reserve missing standard slots before any config, socket or directory
     * can acquire them. Terminal detach/resume deliberately replaces 0..2;
     * those slots must never hold application-owned state. Existing streams
     * and all their descriptor/status flags belong to the launcher. */
    for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; ++fd) {
        if (fcntl(fd, F_GETFD) >= 0) continue;
        if (errno != EBADF) return 0;
        /* A new standard stream must survive exec, so omit O_CLOEXEC. The
         * ascending scan makes this the lowest free descriptor. If another
         * owner takes that slot, fail rather than overwrite its descriptor. */
        int quiet = open("/dev/null", O_RDWR);
        if (quiet < 0) return 0;
        if (quiet != fd) {
            (void)close(quiet);
            errno = EBUSY;
            return 0;
        }
    }
    return 1;
}

/* CLI diagnostics are display-only: never localize option names, paths,
 * IRC wire text or dynamically supplied error strings here. The existing
 * English error and exit status remain the fallback. */
/* Local config errors may contain OS details; preserve them verbatim. */
static void cli_source_diagnostic(FILE *out, const char *raw)
{
    char display[1024];
    fprintf(out, PROGRAM_NAME ": %s\n", irc_security_diagnostic(raw, display, sizeof display));
}

static void cli_diagnostic(FILE *out, const char *english)
{
    fputs(PROGRAM_NAME ": ", out);
    fputs(irc_tr(english), out);
    fputc('\n', out);
}

/* Keep the canonical, copyable option table in English; prepend a small
 * language-specific orientation only when a translation is available. */

/* Substitute only named display fields; never interpret translations as printf
 * formats. The values (option names, server errors and slot numbers) are
 * emitted byte-for-byte, whatever their contents. Reject malformed locale
 * templates and fall back to the fixed English source sentence. */
typedef struct { const char *name, *value; } CliDisplayField;
static int cli_template_valid(const char *s, const CliDisplayField *args, size_t nargs)
{
    size_t counts[2] = {0, 0};
    if (!s || nargs > 2) return 0;
    for (const char *p = s; *p;) {
        if (*p == '}') return 0;
        if (*p != '{') { ++p; continue; }
        const char *end = strchr(p, '}');
        if (!end) return 0;
        int found = 0;
        for (size_t i = 0; i < nargs; ++i) {
            size_t n = strlen(args[i].name);
            if ((size_t)(end - (p + 1)) == n && memcmp(p + 1, args[i].name, n) == 0) {
                if (++counts[i] > 1) return 0;
                found = 1;
                break;
            }
        }
        if (!found) return 0;
        p = end + 1;
    }
    for (size_t i = 0; i < nargs; ++i) if (counts[i] != 1) return 0;
    return 1;
}
static void cli_diagnostic_fields(FILE *out, const char *english,
                                  const CliDisplayField *args, size_t nargs)
{
    const char *s = irc_tr(english);
    if (!cli_template_valid(s, args, nargs)) s = english;
    fputs(PROGRAM_NAME ": ", out);
    for (const char *p = s; *p;) {
        if (*p != '{') { fputc(*p++, out); continue; }
        const char *end = strchr(p, '}');
        for (size_t i = 0; i < nargs; ++i) {
            if (strlen(args[i].name) == (size_t)(end - p - 1) &&
                memcmp(p + 1, args[i].name, (size_t)(end - p - 1)) == 0) {
                fputs(args[i].value, out);
                break;
            }
        }
        p = end + 1;
    }
    fputc('\n', out);
}

static void cli_localized_intro(FILE *out)
{
    static const char *const english[] = {
        "Connect with --server HOST; use --no-connect to start without a network connection." ,
        "Choose verified TLS with --tls; --plain sends IRC without encryption." ,
        "Use --check-config to check settings; it does not test login or TLS." ,
        "Use --sessions to list jobs; --resume JOB resumes a detached job." ,
        "Use signals is off by default; Ctrl-C/Ctrl-Z cancel unsent text. Use /quit to exit." ,
        "Run --manual for the full manual in your selected language." ,
        "Command and option spellings below are literal." ,
    };
    const char *heading = irc_tr("Command-line overview (option spellings remain unchanged):");
    if (strcmp(heading, "Command-line overview (option spellings remain unchanged):") == 0) return;
    fputs(heading, out);
    fputc('\n', out);
    for (size_t i = 0; i < sizeof english / sizeof english[0]; ++i) {
        fputs("  ", out);
        fputs(irc_tr(english[i]), out);
        fputc('\n', out);
    }
    fputc('\n', out);
}

/* Keep option spellings and arguments byte-for-byte. Only the explanatory
 * description is translated; no translated text reaches getopt or config. */
typedef struct { const char *syntax; const char *description; } CliUsageOption;
static void cli_usage_options(FILE *out, const CliUsageOption *items, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        fputs(items[i].syntax, out);
        fputs(irc_tr(items[i].description), out);
        fputc('\n', out);
    }
}

/* Only display prose is localized. Never translate shell flags, IRC commands,
 * configuration keys, filenames, or typed input. */
static void cli_usage_line(FILE *out, const char *english)
{
    fputs(irc_tr(english), out);
    fputc('\n', out);
}

static void cli_version(FILE *out)
{
    fputs(PROGRAM_NAME " " PROGRAM_VERSION " [lang-rtl-1.0-a]\n", out);
    cli_usage_line(out, "An application of ThatIAm.vip (www.thatiam.vip), designed by remfan1994.");
    cli_usage_line(out, "License: IALA (Insha-Allah License Agreement); see the included license text.");
    cli_usage_line(out, "Source available under the included license.");
    fputs("https://www.thatiam.vip\n", out);
}

static void idle_usage(FILE *out)
{
    fputs(irc_tr("Usage: "), out);
    fputs(PROGRAM_NAME, out);
#ifndef ONEKEY_ONLY
    fputs(" -i", out);
#endif
    fputs(" [OPTION]...\n", out);
    fputs(irc_tr("A small IRC client for Unix-like terminals."), out);
    fputs("\n\n", out);
    cli_usage_line(out, "An application of ThatIAm.vip (www.thatiam.vip), designed by remfan1994.");
    cli_usage_line(out, "License: IALA (Insha-Allah License Agreement); see the included license text.");
    fputc('\n', out);
    static const CliUsageOption options[] = {
        {"  -i, --idle            ", "Use the minimalist command interface"},
#ifndef ONEKEY_ONLY
        {"  -N, --ncurses         ", "Use the full-screen ncurses interface"},
#endif
        {"  -s, --server HOST     ", "IRC server; omit for a disconnected workspace"},
        {"  -p, --port PORT       ", "TCP port (TLS 6697; plain 6667)"},
        {"  -c, --channel LIST    ", "Comma-separated channels; may repeat"},
        {"  -n, --nick NICK       ", "IRC nickname; defaults to system login"},
        {"  -C, --config FILE     ", "Primary config file and saved server profiles"},
        {"      --tls             ", "Verified TLS connection (default)"},
        {"      --plain           ", "Explicitly use unencrypted IRC"},
        {"      --ca-file FILE    ", "Custom CA PEM file (regular, nonempty, at most 16 MiB)"},
        {"      --no-connect      ", "Start without connecting to a server"},
        {"      --background      ", "Run detached without a terminal"},
        {"      --reconnect       ", "Retry interrupted connections with backoff"},
        {"      --test-mode       ", "Simulate IRC activity offline without saving"},
        {"      --resume [JOB]    ", "Resume a detached job by number, or select one"},
        {"      --sessions        ", "List local background jobs"},
        {"      --detach-session N ", "Release a job terminal; keep the worker running"},
        {"      --close-session N ", "Close an entire local background job"},
        {"      --init-config     ", "Create a config file; never overwrite"},
        {"      --check-config    ", "Validate config and saved server profiles"},
        {"      --manual          ", "Print the complete built-in manual"},
        {"  -h, --help            ", "Print help and exit"},
        {"  -v, --version         ", "Print version and exit (-V also accepted)"},
    };
    cli_usage_options(out, options, sizeof options / sizeof options[0]);
    fputc('\n', out);
    cli_usage_line(out, "In log view, / opens Command; ordinary typing opens Compose.");
    cli_usage_line(out, "Enter sends by default; [input] send_mode=review adds a review step.");
    cli_usage_line(out, "Esc leaves an editor/reader; in Review it restores the draft; in the log it backgrounds.");
    cli_usage_line(out, "Use signals is off by default: Ctrl-C/Ctrl-Z cancel a draft, not the program; Ctrl-D is inert.");
    cli_usage_line(out, "[input] use_signals=false is the default; true enables tty Ctrl-C/Ctrl-Z signals.");
    cli_usage_line(out, "Tab completes. Command Up recalls accepted commands; Compose arrows edit.");
    fputs("\n", out);
    cli_usage_line(out, "Chat defaults to [HH:MM:SS] <nick> text. /set output_format full adds context; /set output_format compact restores concise output.");
    cli_usage_line(out, "/save output keeps this choice. Help opens a separate reader.");
    fputs("\n", out);
    cli_usage_line(out, "Start with /connect irc.example.org, then /join #channel.");
    cli_usage_line(out, "/saveconf remembers your settings; next launch, /connect reuses them.");
    cli_usage_line(out, "Optional guided setup: /quickstart or /panel.");
    cli_usage_line(out, "Optional hotkeys: /bindings, /bind KEY ACTION, /unbind KEY|all; none by default.");
    cli_usage_line(out, "Lists: /cl or /channels; /ql or /queries; /sl or /servers.");
    cli_usage_line(out, "Help: /help ls lists topics; /help COMMAND explains a command.");
    fprintf(out, "%s /connect /join /part /query /msg /nick /away /back\n", irc_tr("Common commands:"));
    fputs("  /autoconnect /autojoin /autoauth /server /servers /set /saveconf /quit\n", out);
#ifdef ONEKEY_ONLY
    fprintf(out, "%s ONEKEYIRC_LANG=id_ID.UTF-8 (id ms tr es pt fr de ru it ar).\n", irc_tr("Interface language:"));
#else
    fprintf(out, "%s VIPIRC_LANG=id_ID.UTF-8 (id ms tr es pt fr de ru it ar).\n", irc_tr("Interface language:"));
#endif
    cli_usage_line(out, "Locale fallback: LC_ALL, LC_MESSAGES, LANG; IRC commands stay in English.");
    cli_usage_line(out, "Session controls use job numbers from --sessions, not process IDs.");
#ifndef ONEKEY_ONLY
    fprintf(out, "%s [session] default_interface=idle|ncurses.\n", irc_tr("Default interface:"));
    cli_usage_line(out, "Use /set default_interface idle, then /save input, to remember -i.");
    cli_usage_line(out, "Explicit -i or -N selects this launch without saving the override.");
    cli_usage_line(out, "Bare --resume keeps the worker's interface; -i or -N selects one.");
    cli_usage_line(out, "Changing interfaces discards drafts, send reviews, recall and unapplied forms.");
#endif
}


static void usage(FILE *out, int idle_mode)
{
    if (idle_mode) { idle_usage(out); return; }
    fputs(irc_tr("Usage: "), out);
    fputs("vipirc [OPTION]...\n", out);
    fputs(irc_tr("A small IRC client for Unix-like terminals."), out);
    fputs("\n\n", out);
    cli_usage_line(out, "An application of ThatIAm.vip (www.thatiam.vip), designed by remfan1994.");
    cli_usage_line(out, "License: IALA (Insha-Allah License Agreement); see the included license text.");
    fputc('\n', out);
    static const CliUsageOption options[] = {
        {"  -s, --server HOST     ", "IRC server; omit for a disconnected workspace"},
        {"  -p, --port PORT       ", "TCP port (TLS 6697; plain 6667)"},
        {"  -c, --channel LIST    ", "Comma-separated channels; may repeat"},
        {"  -n, --nick NICK       ", "IRC nickname; defaults to system login"},
        {"  -C, --config FILE     ", "Primary config file and saved server profiles"},
        {"  -i, --idle            ", "Use the minimalist command interface"},
        {"  -N, --ncurses         ", "Use the full-screen ncurses interface"},
        {"      --tls             ", "Verified TLS connection (default)"},
        {"      --plain           ", "Explicitly use unencrypted IRC"},
        {"      --ca-file FILE    ", "Custom CA PEM file (regular, nonempty, at most 16 MiB)"},
        {"      --no-connect      ", "Start without connecting to a server"},
        {"      --background      ", "Run detached without a terminal"},
        {"      --reconnect       ", "Retry interrupted connections with backoff"},
        {"      --test-mode       ", "Simulate IRC activity offline without saving"},
        {"      --resume [JOB]    ", "Resume a detached job by number, or select one"},
        {"      --sessions        ", "List local background jobs"},
        {"      --detach-session N ", "Release a job terminal; keep the worker running"},
        {"      --close-session N ", "Close an entire local background job"},
        {"      --init-config     ", "Create a config file; never overwrite"},
        {"      --check-config    ", "Validate config and saved server profiles"},
        {"      --manual          ", "Print the complete built-in manual"},
        {"  -h, --help            ", "Print help and exit"},
        {"  -v, --version         ", "Print version and exit (-V also accepted)"},
    };
    cli_usage_options(out, options, sizeof options / sizeof options[0]);
    fputc('\n', out);
    cli_usage_line(out, "Example:");
    fputs("  vipirc --server irc.ircnow.org --channel '#ircnow' --nick tutankh-aten\n\n", out);
#ifdef ONEKEY_ONLY
    fprintf(out, "%s ONEKEYIRC_LANG=id_ID.UTF-8 (id ms tr es pt fr de ru it ar).\n", irc_tr("Interface language:"));
#else
    fprintf(out, "%s VIPIRC_LANG=id_ID.UTF-8 (id ms tr es pt fr de ru it ar).\n", irc_tr("Interface language:"));
#endif
    cli_usage_line(out, "Locale fallback: LC_ALL, LC_MESSAGES, LANG; IRC commands stay in English.");
    cli_usage_line(out, "Custom hotkeys are optional; none are assigned by default.");
    fprintf(out, "  vipirc (F1) > %s; /bindings\n", irc_tr("Key bindings"));
    cli_usage_line(out, "Common in-app commands:");
    fputs("  /help  /manual  /join #channel [key]  /msg nick text  /users\n", out);
    fputs("  /cl, /channels; /ql, /queries; /sl, /servers\n", out);
    fputs("  /channel 0  /pm nick  /close  /save  /quit\n", out);
    static const CliUsageOption commands[] = {
        {"  /clearinput       ", "Clear this conversation's draft"},
        {"  /servers          ", "List server connections"},
        {"  /dcc, /transfers  ", "File transfers"},
        {"  /mentions        ", "Mentions collect messages that matched your nickname or watch terms. Their original conversations remain separate."},
        {"  /unread          ", "Open grouped unread conversations and mentions across servers"},
        {"  /background      ", "Keep running and release this terminal; resume with vipirc --resume"},
    };
    cli_usage_options(out, commands, sizeof commands / sizeof commands[0]);
    fputs("  /away [message]  /back  /ignore [mask]  /unignore mask\n", out);
    cli_usage_line(out, "Terminal controls:");
    cli_usage_line(out, "Chat starts ready to type; arrows edit and hjkl are literal letters.");
    cli_usage_line(out, "Each function key opens the indicated panel.");
    fprintf(out, "  F1 vipirc; F2 %s; F3 %s; F4 %s;\n  F5 %s; F6 %s.\n",
            irc_tr("Servers"), irc_tr("Channels"), irc_tr("Queries"),
            irc_tr("Mentions/unread"), irc_tr("File transfers"));
    cli_usage_line(out, "Esc in a private conversation opens the private conversation list; in other chat views it opens vipirc.");
    cli_usage_line(out, "Esc returns from settings and application information to vipirc; in an ordinary panel it returns to chat. Arrow keys navigate within the panel.");
    cli_usage_line(out, "Empty categories are omitted; vipirc is always available. Counts appear in parentheses after category names.");
    cli_usage_line(out, "Tab completes nicknames in the chat field.");
    cli_usage_line(out, "The vipirc (F1) menu contains the following entries:");
    fprintf(out, "  %s, %s, %s, %s, %s,\n  %s, %s, %s, %s.\n",
            irc_tr("Server"), irc_tr("Nickname"), irc_tr("Settings"),
            irc_tr("Key bindings"), irc_tr("Manual"), irc_tr("Command"),
            irc_tr("Background"), irc_tr("About"), irc_tr("Insha-Allah license agreement"));
    cli_usage_line(out, "The manual returns to the screen that opened it; / searches, n/N finds next/previous.");
    cli_usage_line(out, "Press / in vipirc (F1), or at the beginning of the chat field, to open the command prompt.");
    cli_usage_line(out, "In the command prompt, Right completes names; Up/Down recalls matching history. PgUp/PgDn browses all suggestions.");
    cli_usage_line(out, "Enter executes the command; Esc cancels without changing the chat draft. The corresponding on-screen buttons do the same.");
    cli_usage_line(out, "In lists, actions appear beneath the heading. Up/Down selects an item; Left/Right selects its action. Both remain highlighted; Enter executes the selected action.");
    cli_usage_line(out, "Opening a server from its list opens its MOTD view. Delete closes the selected server, channel or private conversation view.");
    cli_usage_line(out, "In vipirc (F1), b returns to chat; Esc asks for confirmation before quitting. Only Y/y confirms; other keys cancel. Job controls remain native.");
    cli_usage_line(out, "Enter submits chat; /clearinput clears the draft. In mouse mode, two buttons below the text field clear or send it.");
    cli_usage_line(out, "/users toggles the channel roster. PgUp/PgDn scrolls chat history.");
    cli_usage_line(out, "Settings let you edit automatic connections, channel joins, authentication and preferences.");
    cli_usage_line(out, "Disable optional guidance using this setting:");
    fprintf(out, "  %s > %s > %s\n", irc_tr("Settings"), irc_tr("Interface"), irc_tr("Hints"));
    cli_usage_line(out, "Authentication sources: private file, GPG file, or explicit value in a private configuration file.");
    fprintf(out, "%s [session] default_interface=ncurses|idle.\n", irc_tr("Default interface:"));
    cli_usage_line(out, "Use /set default_interface idle, then /save input, to remember -i.");
    cli_usage_line(out, "Explicit -i or -N selects this launch without saving the override.");
    cli_usage_line(out, "Bare --resume keeps the worker's interface; -i or -N selects one.");
    cli_usage_line(out, "Changing interfaces discards drafts, send reviews, recall and unapplied forms.");
    cli_usage_line(out, "Session controls use job numbers from --sessions, not process IDs.");
    cli_usage_line(out, "Read: man vipirc, vipirc --manual, or /help inside the app.");
}

static int copy_option(char *dst, size_t cap, const char *value, const char *name)
{
    if (strlen(value) >= cap) {
        cli_diagnostic_fields(stderr, "{option} is too long",
                              (CliDisplayField[]){{"option", name}}, 1);
        return 0;
    }
    memcpy(dst, value, strlen(value) + 1);
    return 1;
}

static int port_number(const char *s)
{
    char *end;
    long n;
    if (!*s) return 0;
    errno = 0;
    n = strtol(s, &end, 10);
    return !errno && !*end && n > 0 && n <= 65535 ? (int)n : 0;
}

typedef struct ConnectionOptions {
    char server[256], nick[64], channels[1024], ca_file[CONFIG_PATH_MAX];
    int server_set, nick_set, channels_set, ca_set, port, tls;
} ConnectionOptions;

static int connection_options_present(const ConnectionOptions *options)
{
    return options->server_set || options->nick_set || options->channels_set ||
           options->ca_set || options->port || options->tls >= 0;
}

static void connection_options_apply(Config *config, const ConnectionOptions *options)
{
    if (options->server_set && *config->server &&
        !irc_equal(config->server, options->server, IRC_CASE_ASCII)) {
        /* A command-line destination never inherits another host's secrets. */
        strcpy(config->auth, "none");
        config->auth_user[0] = config->password_file[0] = config->server_password_file[0] = 0;
        config->password_gpg_file[0] = config->server_password_gpg_file[0] = 0;
        config_wipe(config->password, sizeof config->password);
        config_wipe(config->server_password, sizeof config->server_password);
        config->autocommands.count = 0;
        config_wipe(config->autocommands.lines, sizeof config->autocommands.lines);
        config->channels[0] = config->ca_file[0] = 0;
        config->bouncer_type = CONFIG_BOUNCER_NONE;
        config->bouncer_away = 0;
        config->bouncer_user[0] = config->bouncer_network[0] = config->bouncer_client[0] = 0;
    }
    if (options->server_set) snprintf(config->server, sizeof config->server, "%s", options->server);
    if (options->nick_set) snprintf(config->nick, sizeof config->nick, "%s", options->nick);
    if (options->channels_set) snprintf(config->channels, sizeof config->channels, "%s", options->channels);
    if (options->ca_set) snprintf(config->ca_file, sizeof config->ca_file, "%s", options->ca_file);
    if (options->tls >= 0) {
        if (!options->port && config->tls != options->tls)
            config->port = options->tls ? 6697 : 6667;
        config->tls = options->tls;
    }
    if (options->port) config->port = options->port;
}

static int prompt(char *dst, size_t cap, const char *label, const char *def)
{
    char line[4096];
    char safe_label[512], safe_default[512];
    size_t len = 0;
    int overflow = 0, invalid = 0;
    irc_sanitize(safe_label, sizeof safe_label, label);
    irc_sanitize(safe_default, sizeof safe_default, def);
    if (*def) printf("%s: (%s) ", safe_label, safe_default);
    else printf("%s: ", safe_label);
    fflush(stdout);
    for (;;) {
        int ch = getchar();
        if (ch == EOF) {
            if (ferror(stdin)) {
                if (errno != EINTR) return 0;
                clearerr(stdin);
                continue;
            }
            /* In a canonical terminal VEOF ends the current read, not the
             * lifetime of this prompt. Keep any text already received and
             * wait for a later Enter. A real hangup/error still ends promptly;
             * polling before retrying also prevents an EOF busy loop. */
            clearerr(stdin);
            struct pollfd input = { STDIN_FILENO, POLLIN, 0 };
            int ready;
            do { ready = poll(&input, 1, -1); } while (ready < 0 && errno == EINTR);
            if (ready < 0 || (input.revents & (POLLHUP | POLLERR | POLLNVAL)) ||
                !isatty(STDIN_FILENO)) return 0;
            continue;
        }
        if (ch == '\n') break;
        /* If VEOF is mapped elsewhere, literal Ctrl-D is still inert.
         * VINTR/VQUIT/VSUSP are consumed by the terminal driver, unchanged. */
        if (ch == 4) continue;
        if (!ch) { invalid = 1; continue; }
        if (len + 1 < sizeof line) line[len++] = (char)ch;
        else overflow = 1;
    }
    if (overflow) {
        cli_diagnostic(stderr, "input is too long");
        return -1;
    }
    if (invalid) {
        cli_diagnostic(stderr, "input contains an unsupported control character");
        return -1;
    }
    line[len] = '\0';
    if (len && line[len - 1] == '\r') line[--len] = '\0';
    return copy_option(dst, cap, len ? line : def, label) ? 1 : -1;
}

/* Informational and session-control invocations must finish before
 * normal startup acquires a terminal or creates live network state. A
 * frontend switch is a return to this driver, not process teardown. */
int main(int argc, char **argv)
{
#ifndef ONEKEY_ONLY
    app_manual_set_executable_path(argv[0]);
#endif

    signal(SIGPIPE, SIG_IGN);
    if (!reserve_standard_descriptors()) {
        cli_diagnostic(stderr, "cannot reserve standard input/output descriptors");
        return 1;
    }
    Config cfg;
    char path[CONFIG_PATH_MAX], error[512];
    char login[64] = "user";
    ConnectionOptions connection = { .tls = -1 };
    int ch, explicit_config = 0;
    int no_connect = 0, init_config = 0, check_config = 0;
    int start_background = 0, reconnect = 0;
    int startup_connect, loaded, result = 0, test_mode = 0;
    int idle_mode = DEFAULT_IDLE, interface_override = -1, information = 0;
    const char *resume_path = NULL;
    const char *close_session = NULL, *detach_session = NULL;
    int resume_requested = 0, list_sessions = 0, close_requested = 0, detach_requested = 0;
    int resume_other_options = 0;
    struct passwd *pw;
    static const struct option options[] = {
        {"help", no_argument, NULL, 'h'}, {"version", no_argument, NULL, 'v'},
        {"server", required_argument, NULL, 's'},
        {"port", required_argument, NULL, 'p'},
        {"channel", required_argument, NULL, 'c'},
        {"nick", required_argument, NULL, 'n'},
        {"config", required_argument, NULL, 'C'},
        {"idle", no_argument, NULL, 'i'},
        {"ncurses", no_argument, NULL, 'N'},
        {"tls", no_argument, NULL, 1000},
        {"plain", no_argument, NULL, 1001},
        {"ca-file", required_argument, NULL, 1002},
        {"no-connect", no_argument, NULL, 1003},
        {"init-config", no_argument, NULL, 1004},
        {"check-config", no_argument, NULL, 1005},
        {"manual", no_argument, NULL, 1006},
        {"test-mode", no_argument, NULL, 1007},
        {"resume", optional_argument, NULL, 1008},
        {"sessions", no_argument, NULL, 1009},
        {"background", no_argument, NULL, 1010},
        {"reconnect", no_argument, NULL, 1011},
        {"close-session", required_argument, NULL, 1012},
        {"detach-session", required_argument, NULL, 1013},
        {NULL, 0, NULL, 0}
    };

    if (!setlocale(LC_ALL, "")) (void)setlocale(LC_ALL, "C");
#ifdef ONEKEY_ONLY
    config_minimal_defaults(&cfg);
#else
    config_defaults(&cfg);
#endif
    path[0] = '\0';
    opterr = 0;
    /* Locate the configuration without applying options twice. getopt_long
     * consumes option values, so values named --config are not misinterpreted. */
    while ((ch = getopt_long(argc, argv, "+hiNvs:p:c:n:C:V", options, NULL)) != -1) {
        if (ch == 'i') {
            idle_mode = interface_override = 1;
            continue;
        }
        if (ch == 'N') {
#ifdef ONEKEY_ONLY
            cli_diagnostic(stderr, "the ncurses interface is available in vipirc; onekeyirc uses the minimalist interface");
            return 2;
#else
            idle_mode = interface_override = 0;
            continue;
#endif
        }
        if (ch == 'h' || ch == 'v' || ch == 'V' || ch == 1006) {
            if (!information) information = ch;
            continue;
        }
        if (ch == 1008) {
            if (resume_requested) {
                cli_diagnostic(stderr, "specify --resume only once");
                return 2;
            }
            resume_requested = 1;
            resume_path = optarg;
            if (!resume_path && optind < argc && argv[optind][0] != '-')
                resume_path = argv[optind++];
            continue;
        }
        if (ch == 1009) {
            list_sessions = 1;
            continue;
        }
        if (ch == 1012) {
            if (close_requested) {
                cli_diagnostic(stderr, "specify --close-session only once");
                return 2;
            }
            close_requested = 1;
            close_session = optarg;
            continue;
        }
        if (ch == 1013) {
            if (detach_requested) {
                cli_diagnostic(stderr, "specify --detach-session only once");
                return 2;
            }
            detach_requested = 1;
            detach_session = optarg;
            continue;
        }
        resume_other_options = 1;
        if (ch == 'C') {
            if (!copy_option(path, sizeof path, optarg, "config path")) return 2;
            explicit_config = 1;
        } else if (ch == 1004) init_config = 1;
        else if (ch == 1005) check_config = 1;
        else if (ch == 1007) test_mode = 1;
        else if (ch == 1010) start_background = 1;
        else if (ch == 1011) reconnect = 1;
        else if (ch == '?') {
            cli_diagnostic(stderr, "invalid option; use --help");
            return 2;
        }
    }
    if (optind != argc) {
        cli_diagnostic(stderr, "unexpected operand; use --help");
        return 2;
    }
    if (information == 'h') {
        cli_localized_intro(stdout);
        usage(stdout, idle_mode);
        return 0;
    }
    if (information == 'v' || information == 'V') {
        cli_version(stdout);
        return 0;
    }
    if (information == 1006) {
#ifndef ONEKEY_ONLY
        if (!idle_mode)
            for (const char *const *line = app_selected_manual(); *line; ++line) puts(*line);
        else
#endif
            idle_manual_translated(stdout, PROGRAM_NAME, irc_tr);
        return ferror(stdout) ? 1 : 0;
    }
    /* No configuration or credential is read before these process-wide
     * protections take effect. Informational invocations above remain usable. */
    if (getuid() != geteuid() || getgid() != getegid()) {
        cli_diagnostic(stderr, "set-user-ID/set-group-ID operation is not supported");
        return 1;
    }
    struct rlimit no_core = {0, 0};
    if (setrlimit(RLIMIT_CORE, &no_core) < 0) {
        cli_diagnostic(stderr, "cannot disable credential-bearing core dumps");
        return 1;
    }
#ifdef __linux__
    /* Linux core-pipe handlers can bypass RLIMIT_CORE. Disable dumpability
     * as well, before any configuration or credential reaches memory. */
    if (prctl(PR_SET_DUMPABLE, 0L, 0L, 0L, 0L) < 0) {
        cli_diagnostic(stderr, "cannot disable process memory dumps");
        return 1;
    }
#endif
    (void)umask(0077);
    /* Management commands operate on an existing worker. Keep this branch
     * before config loading, account lookup and construction of a new Client;
     * a shell job number is unrelated to an in-program server slot. */
    if (resume_requested || list_sessions || close_requested || detach_requested) {
        if (resume_other_options || resume_requested + list_sessions + close_requested + detach_requested > 1) {
            cli_diagnostic(stderr, "--resume, --sessions, --detach-session and --close-session are separate operations; do not combine them with connection or configuration options");
            return 2;
        }
        if (detach_requested) return background_detach_session(detach_session);
        if (close_requested) return background_close_session(close_session);
        return list_sessions ? background_sessions() :
#ifdef ONEKEY_ONLY
               background_resume(resume_path, BACKGROUND_FRONTEND_IDLE);
#else
               background_resume(resume_path, interface_override < 0 ? BACKGROUND_FRONTEND_CURRENT :
                                 interface_override ? BACKGROUND_FRONTEND_IDLE : BACKGROUND_FRONTEND_FULL);
#endif
    }
    if (test_mode && (init_config || check_config)) {
        cli_diagnostic(stderr, "--test-mode cannot be combined with --init-config or --check-config");
        return 2;
    }
    if ((start_background || reconnect) && (init_config || check_config)) {
        cli_diagnostic(stderr, "--background and --reconnect are runtime options; do not combine them with --init-config or --check-config");
        return 2;
    }
    /* Informational options above do not depend on home-directory or account
     * discovery, which may involve a system account service. */
    if (!explicit_config && (cfg.minimal ?
        config_minimal_path(path, sizeof path, error, sizeof error) :
        config_default_path(path, sizeof path, error, sizeof error)) < 0) {
        cli_source_diagnostic(stderr, error);
        return 2;
    }
    if (!*path) {
        cli_diagnostic(stderr, "configuration path must not be empty");
        return 2;
    }
    loaded = config_load(&cfg, path, error, sizeof error);
    if (loaded < 0 || (loaded == 0 && explicit_config && !init_config)) {
        cli_source_diagnostic(stderr, loaded < 0 ? error : irc_tr("configuration file does not exist; use --init-config first"));
        return 2;
    }
#ifndef ONEKEY_ONLY
    /* The primary config owns the default interface. Command-line choices
     * select this invocation without changing the saved preference. A saved
     * secondary server must not replace this application-wide choice. */
    if (interface_override < 0) idle_mode = cfg.default_interface == CONFIG_INTERFACE_IDLE;
#else
    (void)interface_override;
#endif
    /* Second option pass: collect connection overrides without choosing a
     * server slot yet. Client initialization resolves the remembered default;
     * an explicit --server selects primary instead. */
    optind = 1;
#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__APPLE__)
    optreset = 1;
#endif
    while ((ch = getopt_long(argc, argv, "+hiNvs:p:c:n:C:V", options, NULL)) != -1) {
        switch (ch) {
        case 's':
            if (!copy_option(connection.server, sizeof connection.server, optarg, "server")) return 2;
            connection.server_set = 1;
            break;
        case 'p':
            connection.port = port_number(optarg);
            if (!connection.port) {
                cli_diagnostic(stderr, "port must be 1..65535");
                return 2;
            }
            break;
        case 'c':
            connection.channels_set = 1;
            if (strlen(connection.channels) + strlen(optarg) + 2 > sizeof connection.channels) {
                cli_diagnostic(stderr, "channel list is too long");
                return 2;
            }
            if (*connection.channels) strcat(connection.channels, ",");
            strcat(connection.channels, optarg);
            break;
        case 'n':
            if (!copy_option(connection.nick, sizeof connection.nick, optarg, "nick")) return 2;
            connection.nick_set = 1;
            break;
        case 1000: connection.tls = 1; break;
        case 1001: connection.tls = 0; break;
        case 1002:
            if (!copy_option(connection.ca_file, sizeof connection.ca_file, optarg, "CA path")) return 2;
            connection.ca_set = 1;
            break;
        case 1003: no_connect = 1; break;
        case 1005: check_config = 1; break;
        default: break;
        }
    }
    int runtime_options = !init_config && !check_config;
    {
        Config checked;
        if (runtime_options && !connection.server_set) {
            /* Only check option syntax here. A remembered secondary may have
             * different TLS/auth settings from the primary configuration. */
            if (cfg.minimal) config_minimal_defaults(&checked);
            else config_defaults(&checked);
        } else checked = cfg;
        connection_options_apply(&checked, &connection);
        int valid = config_validate(&checked, error, sizeof error);
        if (valid > 0 && !runtime_options) cfg = checked;
        config_wipe(&checked, sizeof checked);
        if (valid < 0) {
            cli_source_diagnostic(stderr, error);
            return 2;
        }
    }
    if (init_config) {
        if (loaded) {
            cli_diagnostic(stderr, "config already exists; --init-config will not overwrite it");
            return 1;
        }
        if (config_create(&cfg, path, error, sizeof error) < 0) {
            cli_source_diagnostic(stderr, error); return 1;
        }
        printf("%s %s\n", irc_tr("Created"), path);
        return 0;
    }
    if (check_config) {
        if ((cfg.minimal ? config_minimal_profiles_check(path, error, sizeof(error)) :
            config_profiles_check(path, error, sizeof(error))) < 0) {
            cli_source_diagnostic(stderr, error); return 2;
        }
        puts(irc_tr("Configuration is valid.")); return 0;
    }
    if (start_background && !loaded && !connection.server_set && !test_mode) {
        cli_diagnostic(stderr, "--background requires an existing configuration or an explicit --server; configure and check it first");
        return 2;
    }
    if (!start_background && (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))) {
        cli_diagnostic(stderr, "interactive operation requires a terminal; use --help or --manual");
        return 2;
    }
    pw = getpwuid(getuid());
    if (pw && pw->pw_name && strlen(pw->pw_name) < sizeof login)
        snprintf(login, sizeof login, "%s", pw->pw_name);
    int welcome = 1;
#ifndef ONEKEY_ONLY
    if (!start_background) welcome = idle_mode ?
        idle_welcome(PROGRAM_NAME, cfg.use_signals) :
        ui_welcome(cfg.use_signals);
#else
    if (!start_background) welcome = idle_welcome(PROGRAM_NAME, cfg.use_signals);
#endif
    if (welcome <= 0) return welcome < 0 ? 128 - welcome : 1;
    if (test_mode) {
        /* These are local display identities. The simulator never resolves
         * either the saved server or this fallback name. */
        if (!*cfg.server) strcpy(cfg.server, "offline.invalid");
        if (!*cfg.nick) snprintf(cfg.nick, sizeof cfg.nick, "%s",
                                irc_valid_nick(login) ? login : "user");
        if (!*cfg.username) strcpy(cfg.username, "tester");
    }
    int primary_needs_nick = !*cfg.nick;
    if ((idle_mode || start_background) && !*cfg.nick)
        snprintf(cfg.nick, sizeof cfg.nick, "%s", irc_valid_nick(login) ? login : "user");
    if (config_validate(&cfg, error, sizeof error) < 0) {
        cli_source_diagnostic(stderr, error); return 2;
    }
    /* Construct the workspace before resolving profile-specific overrides.
     * After successful initialization, early returns must release both its
     * owned contents and this outer allocation. Frontends borrow it later. */
    Client *client = calloc(1, sizeof(*client));
    if (!client) {
        cli_diagnostic(stderr, "cannot allocate client state");
        return 1;
    }
    if (client_init(client, &cfg, path, login, test_mode, loaded > 0, error, sizeof(error)) < 0) {
        cli_source_diagnostic(stderr, error);
        config_wipe(&cfg, sizeof cfg);
        free(client);
        return 1;
    }
    config_wipe(&cfg, sizeof cfg);
    if (runtime_options && connection_options_present(&connection)) {
        if (!connection.server_set && client->default_unavailable) {
            cli_diagnostic(stderr, "saved default connection is unavailable; specify --server HOST or repair the saved configuration before applying connection options");
            client_free(client);
            free(client);
            return 2;
        }
        size_t index = connection.server_set ? 0 : client->current;
        Config selected = client->slots[index].seed;
        connection_options_apply(&selected, &connection);
        if (!*selected.nick)
            snprintf(selected.nick, sizeof selected.nick, "%s", irc_valid_nick(login) ? login : "user");
        if (config_validate(&selected, error, sizeof error) < 0) {
            cli_source_diagnostic(stderr, error);
            config_wipe(&selected, sizeof selected);
            client_free(client);
            free(client);
            return 2;
        }
        client->slots[index].seed = selected;
        config_wipe(&selected, sizeof selected);
    }
    if (connection.server_set) {
        client->current = 0;
        client->default_unavailable = 0;
    }
    /* Preserve the full interface's first-use primary nickname question.
     * A saved secondary uses the shared login fallback; neither an explicit
     * nickname nor unattended startup may prompt for an unrelated primary. */
    if (!idle_mode && !start_background && !test_mode &&
        !client->current && primary_needs_nick && !connection.nick_set &&
        *client->slots[0].seed.server) {
        Config *selected = &client->slots[0].seed;
        do {
            int p = prompt(selected->nick, sizeof selected->nick, irc_tr("Set user nick"), login);
            if (!p) {
                client_free(client);
                free(client);
                return 0;
            }
            if (p > 0 && irc_valid_nick(selected->nick)) break;
            puts(irc_tr("Choose a valid IRC nickname (ASCII letters, digits, or IRC nickname symbols)."));
            selected->nick[0] = '\0';
        } while (1);
    }
    startup_connect = test_mode || connection.server_set || client->slots[0].seed.autoconnect;
    if (!*client->slots[0].seed.server) startup_connect = 0;
    if (start_background) {
        if (*client->catalog_error) {
            cli_diagnostic_fields(stderr, "cannot start in background: saved-server catalog: {error}",
                                  (CliDisplayField[]){{"error", client->catalog_error}}, 1);
            client_free(client);
            free(client);
            return 2;
        }
        for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
            ClientSlot *slot = &client->slots[i];
            if (slot->used && slot->load_failed) {
                char server_number[24];
                (void)snprintf(server_number, sizeof server_number, "%zu", i + 1u);
                cli_diagnostic_fields(stderr, "cannot start in background: saved server {number}: {error}",
                                      (CliDisplayField[]){{"number", server_number}, {"error", slot->error}}, 2);
                client_free(client);
                free(client);
                return 2;
            }
        }
    }
    client_set_reconnect(client, reconnect);
    if (!start_background)
        (void)client_startup(client, startup_connect, no_connect, error, sizeof(error));

    /* These owners survive a frontend switch. A frontend result of 1000
     * requests replacement; every other result proceeds to final teardown. */
    Background *session_background = background_create();
    DccManager *session_dcc = dcc_create(client->test_mode);
    if (!session_background || !session_dcc) {
        cli_diagnostic(stderr, "cannot allocate session runtime");
        background_free(session_background);
        dcc_free(session_dcc);
        client_free(client);
        free(client);
        return 1;
    }
    background_set_program(session_background, PROGRAM_NAME);
#ifndef ONEKEY_ONLY
    int frontend = idle_mode ? BACKGROUND_FRONTEND_IDLE : BACKGROUND_FRONTEND_FULL;
#endif
    int first = 1;
    for (;;) {
        int next_frontend = BACKGROUND_FRONTEND_CURRENT;
#ifndef ONEKEY_ONLY
        if (frontend == BACKGROUND_FRONTEND_FULL)
            result = ui_run_session(client, session_background, session_dcc,
                                    first && start_background, startup_connect, no_connect, &next_frontend);
        else
#endif
            result = idle_run_session(client, PROGRAM_NAME, session_background, session_dcc,
                                      first && start_background, startup_connect, no_connect, &next_frontend);
        first = 0;
        if (result != 1000) break;
#ifndef ONEKEY_ONLY
        if (next_frontend != BACKGROUND_FRONTEND_FULL && next_frontend != BACKGROUND_FRONTEND_IDLE) {
            result = 1;
            break;
        }
        frontend = next_frontend;
#else
        result = 1;
        break;
#endif
    }
    /* A remote close completes only after all IRC and transfer descriptors
     * have been released. Keep the session identity/lease until that point. */
    dcc_free(session_dcc);
    client_free(client);
    free(client);
    background_finish(session_background, result);
    background_free(session_background);
    return result;
}
