#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config.h"
#ifdef __linux__
#include <stdarg.h>
#include <sys/prctl.h>
static int dump_failure, dump_calls;
static int fixture_prctl(int option, ...)
{
    assert(option == PR_SET_DUMPABLE);
    va_list ap;
    va_start(ap, option);
    for (int i = 0; i < 4; ++i) assert(va_arg(ap, unsigned long) == 0);
    va_end(ap);
    ++dump_calls;
    if (dump_failure) { errno = EPERM; return -1; }
    return 0;
}
#endif

static int identity_case, limit_failure, limit_calls, config_calls;
static uid_t fixture_getuid(void) { return 1001; }
static uid_t fixture_geteuid(void) { return identity_case == 1 ? 1002 : 1001; }
static gid_t fixture_getgid(void) { return 1001; }
static gid_t fixture_getegid(void) { return identity_case == 2 ? 1002 : 1001; }
static int fixture_setrlimit(int resource, const struct rlimit *limit)
{
    assert(resource == RLIMIT_CORE && limit->rlim_cur == 0 && limit->rlim_max == 0);
    ++limit_calls;
    if (limit_failure) { errno = EPERM; return -1; }
    return 0;
}
static int fixture_config_load(Config *, const char *, char *, size_t);

#define main security_client_main
#define getuid fixture_getuid
#define geteuid fixture_geteuid
#define getgid fixture_getgid
#define getegid fixture_getegid
#define setrlimit fixture_setrlimit
#define config_load fixture_config_load
#ifdef __linux__
#define prctl fixture_prctl
#endif
#include "../src/main.c"
#undef main
#undef getuid
#undef geteuid
#undef getgid
#undef getegid
#undef setrlimit
#undef config_load
#ifdef __linux__
#undef prctl
#endif

static int fixture_config_load(Config *config, const char *path, char *error, size_t cap)
{
    (void)config;
    (void)path;
    assert(limit_calls == 1 && !limit_failure && !identity_case);
#ifdef __linux__
    assert(dump_calls == 1 && !dump_failure);
#endif
    mode_t mask = umask(0077);
    assert(mask == 0077);
    ++config_calls;
    snprintf(error, cap, "intentional fixture stop before reading configuration");
    return -1;
}

static void reset_options(void)
{
#if defined(__GLIBC__)
    optind = 0;
#else
    optind = 1;
#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__APPLE__)
    optreset = 1;
#endif
#endif
    limit_calls = config_calls = 0;
#ifdef __linux__
    dump_calls = 0;
#endif
}

int main(void)
{
    mode_t saved_mask = umask(0022);
    char *args[] = {"security-client", "--check-config", "--config", "/unused-fixture-config", NULL};
    for (identity_case = 1; identity_case <= 2; ++identity_case) {
        reset_options();
        assert(security_client_main(4, args) == 1);
        assert(!limit_calls && !config_calls);
    }
    identity_case = 0;
    limit_failure = 1;
    reset_options();
    assert(security_client_main(4, args) == 1);
    assert(limit_calls == 1 && !config_calls);
    limit_failure = 0;
#ifdef __linux__
    dump_failure = 1;
    reset_options();
    assert(security_client_main(4, args) == 1);
    assert(limit_calls == 1 && dump_calls == 1 && !config_calls);
    dump_failure = 0;
#endif
    reset_options();
    assert(security_client_main(4, args) == 2);
    assert(limit_calls == 1 && config_calls == 1);
    char *info[] = {"security-client", "--version", NULL};
    reset_options();
    assert(security_client_main(2, info) == 0);
    assert(!limit_calls && !config_calls);
    (void)umask(saved_mask);
    puts("process security: identity and core-dump guards precede configuration reads; safe failure and information paths passed");
    return 0;
}
