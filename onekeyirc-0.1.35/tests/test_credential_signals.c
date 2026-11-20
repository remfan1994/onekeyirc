/* Child-ownership admission, with real helpers and private files. A wrapper
 * counts production fork calls; the helper remains the existing executable
 * fixture rather than GnuPG or a user's keyring. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/types.h>
#include <unistd.h>
static pid_t counted_fork(void);
#define fork counted_fork
#include "credentials.c"
#undef fork
#define main existing_credential_suite
#include "test_credentials.c"
#undef main

static unsigned forks_started;
static pid_t counted_fork(void) { ++forks_started; return fork(); }
static void nonreaping_handler(int signal_number) { (void)signal_number; }

int main(int argc, char **argv)
{
    if (!strcmp(argv[0], "gpg")) return fake_gpg(argc, argv);
    char directory[] = "/tmp/irc-credential-signals-XXXXXX";
    char executable[CONFIG_PATH_MAX], program[CONFIG_PATH_MAX], source[CONFIG_PATH_MAX];
    char destination[CONFIG_PATH_MAX], error[512];
    assert(realpath(argv[0], executable) && mkdtemp(directory));
    assert(snprintf(program, sizeof program, "%s/gpg", directory) > 0);
    assert(snprintf(source, sizeof source, "%s/input.gpg", directory) > 0);
    assert(snprintf(destination, sizeof destination, "%s/output.gpg", directory) > 0);
    assert(!symlink(executable, program) && !setenv("PATH", directory, 1));
    file_write(source, "hang", 4, 0600);
    struct sigaction original, action;
    assert(!sigaction(SIGCHLD, NULL, &original));
    struct Policy { const char *name; void (*handler)(int); int flags, allowed; } policies[] = {
        {"SIG_IGN", SIG_IGN, 0, 0},
#ifdef SA_NOCLDWAIT
        {"SIG_DFL+SA_NOCLDWAIT", SIG_DFL, SA_NOCLDWAIT, 0},
        {"handler+SA_NOCLDWAIT", nonreaping_handler, SA_NOCLDWAIT, 0},
#endif
        {"SIG_DFL", SIG_DFL, 0, 1},
        {"non-reaping handler", nonreaping_handler, SA_NOCLDSTOP, 1}
    };
    int okay = 1;
    for (size_t i = 0; i < sizeof policies / sizeof policies[0]; ++i) {
        memset(&action, 0, sizeof action); sigemptyset(&action.sa_mask);
        action.sa_handler = policies[i].handler; action.sa_flags = policies[i].flags;
        assert(!sigaction(SIGCHLD, &action, NULL));
        for (int encrypt = 0; encrypt < 2; ++encrypt) {
            unsigned before_forks = forks_started;
            int before_fds = descriptors();
            error[0] = 0;
            Credential *r = encrypt ? NULL : credential_start(source, milliseconds(), error, sizeof error);
            CredentialCreate *w = encrypt ? credential_create_start(destination, fingerprint, "hang", milliseconds(), error, sizeof error) : NULL;
            int started = r || w;
            if (r) credential_cancel(r);
            if (w) credential_create_cancel(w);
            reap_all();
            unsigned made = forks_started - before_forks;
            if (started != policies[i].allowed || made != (unsigned)policies[i].allowed) okay = 0;
            if (!started) assert(strstr(error, "SIGCHLD"));
            assert(descriptors() == before_fds && access(destination, F_OK) < 0);
            printf("%s %s: started=%d forks=%u expected=%d; descriptors restored; destination absent\n",
                   encrypt ? "encrypt" : "decrypt", policies[i].name, started, made, policies[i].allowed);
        }
    }
    assert(!sigaction(SIGCHLD, &original, NULL));
    assert(!unlink(source) && !unlink(program) && !rmdir(directory));
    if (!okay) fputs("FAIL: automatic child reaping was admitted before helper launch\n", stderr);
    return okay ? 0 : 1;
}
