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

static void idle_usage(FILE *out)
{
    fputs("Usage: " PROGRAM_NAME, out);
#ifndef ONEKEY_ONLY
    fputs(" -i", out);
#endif
    fputs(" [OPTION]...\n"
          "A small command-oriented IRC client for Unix-like terminals.\n\n"
          "An application of ThatIAm.vip (www.thatiam.vip).\n"
          "Designed by remfan1994; IALA (Insha-Allah License Agreement).\n\n"
          "  -i, --idle            Use the minimalist interface\n"
#ifndef ONEKEY_ONLY
          "  -N, --ncurses         Use the ncurses interface for this launch\n"
#endif
          "  -s, --server HOST     Server; omit for a disconnected workspace\n"
          "  -p, --port PORT       TCP port (TLS 6697; plain 6667)\n"
          "  -c, --channel LIST    Comma-separated channels; may repeat\n"
          "  -n, --nick NICK       Nickname; defaults to system login or user\n"
          "  -C, --config FILE     Primary config and saved server catalog\n"
          "      --tls             Verified TLS (default)\n"
          "      --plain           Explicitly use unencrypted IRC\n"
          "      --ca-file FILE    Custom CA PEM: regular, nonempty, <=16 MiB\n"
          "      --no-connect      Do not connect at startup\n"
          "      --background      Start detached without a terminal\n"
          "      --reconnect       Retry interrupted connections with backoff\n"
          "      --test-mode       Offline simulation; no credentials or saves\n"
          "      --resume [JOB]    Resume one detached job, or choose its number\n"
          "      --sessions        List local background jobs\n"
          "      --detach-session N Detach a job terminal; keep IRC alive\n"
          "      --close-session N Close one whole local background job\n"
          "      --init-config     Create config; never overwrite\n"
          "      --check-config    Validate config and saved profiles\n"
          "      --manual          Print the complete minimalist manual\n"
          "  -h, --help            Print help and exit\n"
          "  -v, --version         Print version and exit (-V also accepted)\n\n"
          "In log view, / opens Command; ordinary typing opens Compose.\n"
          "Enter sends by default; [input] send_mode=review adds a review step.\n"
          "Esc leaves an editor/reader; in Review it restores the draft; in the log it backgrounds.\n"
          "Use signals is off by default: Ctrl-C/Ctrl-Z cancel a draft, not the program; Ctrl-D is inert.\n"
          "[input] use_signals=false is the default; true enables tty Ctrl-C/Ctrl-Z signals.\n"
          "Tab completes. Command Up recalls accepted commands; Compose arrows edit.\n\n"
          "Chat defaults to [HH:MM:SS] <nick> text. /set output_format full\n"
          "adds context; /set output_format compact restores concise output.\n"
          "/save output keeps this choice. Help opens a separate reader.\n\n"
          "Start with /connect irc.example.org, then /join #channel.\n"
          "/saveconf remembers your settings; next launch, /connect reuses them.\n"
          "Optional guided setup: /quickstart or /panel.\n"
          "Optional hotkeys: /bindings, /bind KEY ACTION, /unbind KEY|all; none by default.\n"
          "Lists: /cl or /channels; /ql or /queries; /sl or /servers.\n"
          "Help: /help ls lists topics; /help COMMAND explains a command.\n"
          "Common commands: /connect /join /part /query /msg /nick /away /back\n"
          "  /autoconnect /autojoin /autoauth /server /servers /set /saveconf /quit\n",
          out);
    fputs("Interface language: VIPIRC_LANG=id_ID.UTF-8 (id ms tr es pt fr de ru it ar).\n"
          "Locale fallback: LC_ALL, LC_MESSAGES, LANG; IRC commands stay in English.\n", out);
    fputs("Session controls use job numbers from --sessions, not process IDs.\n", out);
#ifndef ONEKEY_ONLY
    fputs("Default interface: [session] default_interface=idle|ncurses.\n"
          "Change it with /set default_interface idle, then /save input.\n"
          "Explicit -i or -N selects this launch without saving the override.\n"
          "Bare --resume keeps the worker's interface; -i or -N selects one.\nChanging interfaces discards drafts, send reviews, recall and unapplied forms.\n", out);
#endif
}

static void usage(FILE *out, int idle_mode)
{
    if (idle_mode) { idle_usage(out); return; }
    fputs("Usage: vipirc [OPTION]...\n"
          "A small terminal IRC client for Unix-like systems.\n\n"
          "An application of ThatIAm.vip (www.thatiam.vip).\n"
          "Designed by remfan1994; IALA (Insha-Allah License Agreement).\n\n"
          "  -s, --server HOST     IRC server; omit for a disconnected workspace\n"
          "  -p, --port PORT       TCP port (TLS 6697; plain 6667)\n"
          "  -c, --channel LIST    Channels, comma separated; may repeat\n"
          "  -n, --nick NICK       Nickname; default prompt is system login\n"
          "  -C, --config FILE     Primary config and its saved server catalog\n"
          "  -i, --idle            Command-oriented interface using this config\n"
          "  -N, --ncurses         Use ncurses, overriding the saved default\n"
          "      --tls             Verified TLS (default)\n"
          "      --plain           Explicitly use unencrypted IRC\n"
          "      --ca-file FILE    Custom CA PEM: regular, nonempty, <=16 MiB\n"
          "      --no-connect      Start without connecting any server\n"
          "      --background      Start detached without a terminal\n"
          "      --reconnect       Retry interrupted connections with backoff\n"
          "      --test-mode       Simulate IRC activity offline using this config\n"
          "      --resume [JOB]    Resume one detached job, or choose its number\n"
          "      --sessions        List local background vipirc jobs\n"
          "      --detach-session N Detach a job terminal; keep IRC alive\n"
          "      --close-session N Close one whole local vipirc job\n"
          "      --init-config     Create an editable config; never overwrite\n"
          "      --check-config    Validate primary and saved server profiles\n"
          "      --manual          Print the complete built-in manual\n"
          "  -h, --help            Show this help and exit\n"
          "  -v, --version         Show version and exit (-V is also accepted)\n\n"
          "Example:\n"
          "  vipirc --server irc.ircnow.org --channel '#ircnow' --nick tutankh-aten\n\n",
          out);
    fputs("Interface language: VIPIRC_LANG=id_ID.UTF-8 (id ms tr es pt fr de ru it ar).\n"
          "Locale fallback: LC_ALL, LC_MESSAGES, LANG; IRC commands stay in English.\n", out);
    fputs("Custom hotkeys: App -> Key bindings or /bindings; none assigned by default.\n"
          "Common in-app commands:\n"
          "  /help  /manual  /join #channel [key]  /msg nick text  /users\n"
          "  /cl (or /channels)  /ql (or /queries)  /sl (or /servers)\n"
          "  /channel 0  /pm nick  /close  /save  /quit\n"
          "  /clearinput  Clear this conversation's complete draft\n"
          "  /servers  Server connections    /dcc or /transfers  File transfers\n"
          "  /mentions  Captured nickname and watch-term mentions\n"
          "  /away [message]  /back  /ignore [mask]  /unignore mask\n"
          "  /unread    Combined timestamped mentions and unread conversations\n"
          "  /background  Keep running and return to your shell\n"
          "Terminal controls:\n"
          "  Chat starts ready to type; arrows edit and hjkl are literal letters.\n"
          "  F1 vipirc; F2 Servers; F3 Channels; F4 Queries;\n"
          "  F5 Mentions/unread; F6 File transfers. Each opens its panel.\n"
          "  Esc in a query opens its query list; in other chat it opens vipirc.\n"
          "  Esc in Settings or About returns to vipirc;\n"
          "  Esc in an ordinary panel returns to chat. Arrows stay in the body.\n"
          "  Empty categories are omitted; vipirc is always available.\n"
          "  Category counts appear in parentheses after their names.\n"
          "  Tab completes nicknames in the chat field.\n"
          "  vipirc contains Server/Nick settings, Settings, Manual,\n"
          "  the separate Command prompt, and Background.\n"
          "  About and the Insha-Allah license agreement follow Background.\n"
          "  Manual returns to its opener; / searches, n/N finds next/previous.\n"
          "  / on vipirc (F1), or at chat caret position zero, opens Command.\n"
          "  Commands: Right completes names; Up/Down recalls matching history.\n"
          "  PgUp/PgDn browses all command suggestions.\n"
          "  Enter or Run executes; Esc or Cancel returns without changing chat.\n"
          "  Lists: actions appear beneath the heading. Up/Down selects an item;\n"
          "  Left/Right selects its action. Open on Servers opens its MOTD view.\n"
          "  Both stay highlighted. Enter executes the selected action.\n"
          "  Delete invokes Close in server/channel/query lists.\n"
          "  On vipirc (F1), b returns to chat; Esc asks Really quit? [Y/N].\n"
          "  Only Y/y confirms; another key cancels. Job controls stay native.\n"
          "  Enter submits chat; /clearinput clears the draft. Mouse mode also\n"
          "  shows Clear and Send buttons below the text field.\n"
          "  /users toggles the channel roster. PgUp/PgDn scroll chat history.\n"
          "  Settings edits auto-connect, auto-join, autoauth, and preferences.\n"
          "  Settings > Interface > Hints hides optional guidance when disabled.\n"
          "  Auth sources: private file, GPG file, or explicit private-config value.\n"
          "Default interface: [session] default_interface=ncurses|idle.\n"
          "Use /set default_interface idle, then /save input, to remember -i.\n"
          "Explicit -i or -N selects this launch without saving the override.\n"
          "Bare --resume keeps the worker's interface; -i or -N selects one.\nChanging interfaces discards drafts, send reviews, recall and unapplied forms.\n"
          "Session controls use job numbers from --sessions, not process IDs.\n"
          "Read: man vipirc, vipirc --manual, or /help inside the app.\n",
          out);
}

static int copy_option(char *dst, size_t cap, const char *value, const char *name)
{
    if (strlen(value) >= cap) {
        fprintf(stderr, PROGRAM_NAME ": %s is too long\n", name);
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
        fputs(PROGRAM_NAME ": input is too long\n", stderr);
        return -1;
    }
    if (invalid) {
        fputs(PROGRAM_NAME ": input contains an unsupported control character\n", stderr);
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
    signal(SIGPIPE, SIG_IGN);
    if (!reserve_standard_descriptors()) {
        fputs(PROGRAM_NAME ": cannot reserve standard input/output descriptors\n", stderr);
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
            fputs(PROGRAM_NAME ": the ncurses interface is available in vipirc; onekeyirc uses the minimalist interface\n", stderr);
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
                fputs(PROGRAM_NAME ": specify --resume only once\n", stderr);
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
                fputs(PROGRAM_NAME ": specify --close-session only once\n", stderr);
                return 2;
            }
            close_requested = 1;
            close_session = optarg;
            continue;
        }
        if (ch == 1013) {
            if (detach_requested) {
                fputs(PROGRAM_NAME ": specify --detach-session only once\n", stderr);
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
            fputs(PROGRAM_NAME ": invalid option; use --help\n", stderr);
            return 2;
        }
    }
    if (optind != argc) {
        fputs(PROGRAM_NAME ": unexpected operand; use --help\n", stderr);
        return 2;
    }
    if (information == 'h') {
        usage(stdout, idle_mode);
        return 0;
    }
    if (information == 'v' || information == 'V') {
        puts(PROGRAM_NAME " " PROGRAM_VERSION "\n"
             "An application of ThatIAm.vip (www.thatiam.vip).\n"
             "Designed by remfan1994.\n"
             "License: IALA (Insha-Allah License Agreement).\n"
             "Source available under the included license.\n"
             "https://www.thatiam.vip");
        return 0;
    }
    if (information == 1006) {
#ifndef ONEKEY_ONLY
        if (!idle_mode)
            for (size_t i = 0; vipirc_manual[i]; ++i) puts(vipirc_manual[i]);
        else
#endif
            idle_manual(stdout, PROGRAM_NAME);
        return ferror(stdout) ? 1 : 0;
    }
    /* No configuration or credential is read before these process-wide
     * protections take effect. Informational invocations above remain usable. */
    if (getuid() != geteuid() || getgid() != getegid()) {
        fputs(PROGRAM_NAME ": set-user-ID/set-group-ID operation is not supported\n", stderr);
        return 1;
    }
    struct rlimit no_core = {0, 0};
    if (setrlimit(RLIMIT_CORE, &no_core) < 0) {
        fputs(PROGRAM_NAME ": cannot disable credential-bearing core dumps\n", stderr);
        return 1;
    }
#ifdef __linux__
    /* Linux core-pipe handlers can bypass RLIMIT_CORE. Disable dumpability
     * as well, before any configuration or credential reaches memory. */
    if (prctl(PR_SET_DUMPABLE, 0L, 0L, 0L, 0L) < 0) {
        fputs(PROGRAM_NAME ": cannot disable process memory dumps\n", stderr);
        return 1;
    }
#endif
    (void)umask(0077);
    /* Management commands operate on an existing worker. Keep this branch
     * before config loading, account lookup and construction of a new Client;
     * a shell job number is unrelated to an in-program server slot. */
    if (resume_requested || list_sessions || close_requested || detach_requested) {
        if (resume_other_options || resume_requested + list_sessions + close_requested + detach_requested > 1) {
            fputs(PROGRAM_NAME ": --resume, --sessions, --detach-session and --close-session are separate operations; do not combine them with connection or configuration options\n", stderr);
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
        fputs(PROGRAM_NAME ": --test-mode cannot be combined with --init-config or --check-config\n", stderr);
        return 2;
    }
    if ((start_background || reconnect) && (init_config || check_config)) {
        fputs(PROGRAM_NAME ": --background and --reconnect are runtime options; do not combine them with --init-config or --check-config\n", stderr);
        return 2;
    }
    /* Informational options above do not depend on home-directory or account
     * discovery, which may involve a system account service. */
    if (!explicit_config && (cfg.minimal ?
        config_minimal_path(path, sizeof path, error, sizeof error) :
        config_default_path(path, sizeof path, error, sizeof error)) < 0) {
        fprintf(stderr, PROGRAM_NAME ": %s\n", error);
        return 2;
    }
    if (!*path) {
        fputs(PROGRAM_NAME ": configuration path must not be empty\n", stderr);
        return 2;
    }
    loaded = config_load(&cfg, path, error, sizeof error);
    if (loaded < 0 || (loaded == 0 && explicit_config && !init_config)) {
        fprintf(stderr, PROGRAM_NAME ": %s\n", loaded < 0 ? error : "configuration file does not exist; use --init-config first");
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
                fputs(PROGRAM_NAME ": port must be 1..65535\n", stderr);
                return 2;
            }
            break;
        case 'c':
            connection.channels_set = 1;
            if (strlen(connection.channels) + strlen(optarg) + 2 > sizeof connection.channels) {
                fputs(PROGRAM_NAME ": channel list is too long\n", stderr);
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
            fprintf(stderr, PROGRAM_NAME ": %s\n", error);
            return 2;
        }
    }
    if (init_config) {
        if (loaded) {
            fputs(PROGRAM_NAME ": config already exists; --init-config will not overwrite it\n", stderr);
            return 1;
        }
        if (config_create(&cfg, path, error, sizeof error) < 0) {
            fprintf(stderr, PROGRAM_NAME ": %s\n", error); return 1;
        }
        printf("Created %s\n", path);
        return 0;
    }
    if (check_config) {
        if ((cfg.minimal ? config_minimal_profiles_check(path, error, sizeof(error)) :
            config_profiles_check(path, error, sizeof(error))) < 0) {
            fprintf(stderr, PROGRAM_NAME ": %s\n", error); return 2;
        }
        puts("Configuration is valid."); return 0;
    }
    if (start_background && !loaded && !connection.server_set && !test_mode) {
        fputs(PROGRAM_NAME ": --background requires an existing configuration or an explicit --server; configure and check it first\n", stderr);
        return 2;
    }
    if (!start_background && (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))) {
        fputs(PROGRAM_NAME ": interactive operation requires a terminal; use --help or --manual\n", stderr);
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
        fprintf(stderr, PROGRAM_NAME ": %s\n", error); return 2;
    }
    /* Construct the workspace before resolving profile-specific overrides.
     * After successful initialization, early returns must release both its
     * owned contents and this outer allocation. Frontends borrow it later. */
    Client *client = calloc(1, sizeof(*client));
    if (!client) {
        fputs(PROGRAM_NAME ": cannot allocate client state\n", stderr);
        return 1;
    }
    if (client_init(client, &cfg, path, login, test_mode, loaded > 0, error, sizeof(error)) < 0) {
        fprintf(stderr, PROGRAM_NAME ": %s\n", error);
        config_wipe(&cfg, sizeof cfg);
        free(client);
        return 1;
    }
    config_wipe(&cfg, sizeof cfg);
    if (runtime_options && connection_options_present(&connection)) {
        if (!connection.server_set && client->default_unavailable) {
            fputs(PROGRAM_NAME ": saved default connection is unavailable; specify --server HOST or repair the saved configuration before applying connection options\n", stderr);
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
            fprintf(stderr, PROGRAM_NAME ": %s\n", error);
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
            int p = prompt(selected->nick, sizeof selected->nick, "Set user nick", login);
            if (!p) {
                client_free(client);
                free(client);
                return 0;
            }
            if (p > 0 && irc_valid_nick(selected->nick)) break;
            puts("Choose a valid IRC nickname (ASCII letters, digits, or IRC nickname symbols).");
            selected->nick[0] = '\0';
        } while (1);
    }
    startup_connect = test_mode || connection.server_set || client->slots[0].seed.autoconnect;
    if (!*client->slots[0].seed.server) startup_connect = 0;
    if (start_background) {
        if (*client->catalog_error) {
            fprintf(stderr, PROGRAM_NAME ": cannot start in background: saved-server catalog: %s\n", client->catalog_error);
            client_free(client);
            free(client);
            return 2;
        }
        for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
            ClientSlot *slot = &client->slots[i];
            if (slot->used && slot->load_failed) {
                fprintf(stderr, PROGRAM_NAME ": cannot start in background: saved server %zu: %s\n", i + 1u, slot->error);
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
        fputs(PROGRAM_NAME ": cannot allocate session runtime\n", stderr);
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
