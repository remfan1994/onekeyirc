#define _POSIX_C_SOURCE 200809L
#include "app.h"
#include "config.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const char *const vipirc_manual[] = {"Storage regression fixture", NULL};
static int fail_regular, fail_directory, fail_rename;
static int regular_calls, directory_calls, rename_calls, saved_calls;

int storage_test_fsync(int fd)
{
    struct stat st;
    assert(fstat(fd, &st) == 0);
    if (S_ISDIR(st.st_mode)) {
        if (++directory_calls == fail_directory) { errno = EIO; return -1; }
    } else if (++regular_calls == fail_regular) { errno = ENOSPC; return -1; }
    return fsync(fd);
}

int storage_test_renameat(int source, const char *from, int destination, const char *to)
{
    if (++rename_calls == fail_rename) { errno = EACCES; return -1; }
    return renameat(source, from, destination, to);
}

static void reset_faults(void)
{
    fail_regular = fail_directory = fail_rename = 0;
    regular_calls = directory_calls = rename_calls = 0;
}

static void pause_briefly(long ns)
{
    struct timespec delay = {0, ns};
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
}

static size_t read_file(const char *path, char *out, size_t cap)
{
    FILE *file = fopen(path, "r");
    assert(file);
    size_t size = fread(out, 1, cap, file);
    assert(!ferror(file) && feof(file) && size < cap && !fclose(file));
    return size;
}

static int descriptors(void)
{
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) directory = opendir("/dev/fd");
    if (!directory) return -1;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)))
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) ++count;
    assert(!closedir(directory));
    return count;
}

static void inspect_tree(const char *path, int remove)
{
    DIR *directory = opendir(path);
    struct dirent *entry;
    char child[8192];
    assert(directory);
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        assert(strncmp(entry->d_name, ".vipirc-", 8));
        assert(snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) > 0);
        struct stat st;
        assert(!lstat(child, &st));
        if (S_ISDIR(st.st_mode)) inspect_tree(child, remove);
        else if (remove) assert(!unlink(child));
    }
    assert(!closedir(directory));
    if (remove) assert(!rmdir(path));
}

static void assert_nick(const char *path, const char *nick)
{
    Config loaded;
    char error[512];
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof(error)) == 1 && !strcmp(loaded.nick, nick));
}

static int save_observer(void *context, const Config *config, char *error, size_t cap)
{
    (void)context; (void)config; (void)error; (void)cap;
    ++saved_calls;
    return 1;
}

static void app_validation_atomic(const Config *config, const char *path)
{
    App *app = calloc(1, sizeof(*app)), *before = malloc(sizeof(*before));
    char error[512];
    assert(app && before && app_init(app, config, path));
    app->save_config = save_observer;
    app_log(app, 0, "history must survive rejected settings");
    *before = *app;
    for (int field = 0; field < 3; ++field) {
        for (int save = 0; save < 2; ++save) {
            Config desired = *config;
            desired.history_lines = 50;
            desired.user_list = !desired.user_list;
            if (field == 0) strcpy(desired.password_file, "/tmp/wrong-file ");
            else if (field == 1) { desired.word_count = 1; strcpy(desired.words[0], " phrase"); }
            else strcpy(desired.realname, "name ");
            assert(app_config_apply(app, &desired, save, error, sizeof(error)) == 0);
            assert(strstr(error, "spaces") && !memcmp(app, before, sizeof(*app)) && !saved_calls);
            Buffer *buffer = &app->buffers[0];
            assert(strstr(buffer->lines[buffer->line_start], "history must survive"));
        }
    }
    app_free(app); free(app); free(before);
    puts("settings: six invalid Apply/Save attempts preserved all live state/history and invoked no writer");
}

static void failure_atomicity(const Config *config, const char *path, const char *directory)
{
    Config changed = *config;
    char error[512], before[8192], after[8192], profile_path[CONFIG_PATH_MAX];
    char id[CONFIG_PROFILE_ID_SIZE] = "";
    ConfigProfiles profiles;
    int committed;
    strcpy(changed.nick, "Changed");
    assert(config_save(config, path, error, sizeof(error)) == 1);
    size_t size = read_file(path, before, sizeof(before));
    /* Force a real short file write without filling any filesystem. */
    struct rlimit original_limit, small_limit;
    struct sigaction ignored, original_signal;
    assert(!getrlimit(RLIMIT_FSIZE, &original_limit) && size > 64);
    small_limit = original_limit; small_limit.rlim_cur = 64;
    memset(&ignored, 0, sizeof(ignored)); ignored.sa_handler = SIG_IGN;
    assert(!sigemptyset(&ignored.sa_mask) && !sigaction(SIGXFSZ, &ignored, &original_signal));
    assert(!setrlimit(RLIMIT_FSIZE, &small_limit));
    int short_result = config_save(&changed, path, error, sizeof(error));
    assert(!setrlimit(RLIMIT_FSIZE, &original_limit) && !sigaction(SIGXFSZ, &original_signal, NULL));
    assert(short_result == -1 && strstr(error, "flush"));
    assert(read_file(path, after, sizeof(after)) == size && !memcmp(before, after, size));
    inspect_tree(directory, 0);
    for (int failure = 0; failure < 2; ++failure) {
        reset_faults();
        if (!failure) fail_regular = 1; else fail_rename = 1;
        assert(config_save(&changed, path, error, sizeof(error)) == -1);
        reset_faults();
        assert(read_file(path, after, sizeof(after)) == size && !memcmp(before, after, size));
        inspect_tree(directory, 0);
    }
    reset_faults(); fail_directory = 1;
    assert(config_save(&changed, path, error, sizeof(error)) == -1 && strstr(error, "replaced"));
    reset_faults(); assert_nick(path, "Changed");

    reset_faults(); fail_rename = 2; /* Profile succeeds; adding its catalog entry fails. */
    assert(config_profile_save(config, path, id, &committed, error, sizeof(error)) == -1 && !committed && *id);
    reset_faults();
    assert(config_profiles_load(&profiles, path, error, sizeof(error)) == 0 && !profiles.count);
    assert(config_profile_path(profile_path, sizeof(profile_path), path, id, error, sizeof(error)) == 1);
    assert_nick(profile_path, config->nick);
    assert(config_profile_save(config, path, id, &committed, error, sizeof(error)) == 1 && committed);
    assert(config_profiles_load(&profiles, path, error, sizeof(error)) == 1 && profiles.count == 1);

    reset_faults(); fail_directory = 1;
    assert(config_profile_save(&changed, path, id, &committed, error, sizeof(error)) == -1 && committed);
    reset_faults(); assert_nick(profile_path, "Changed");
    assert(config_profiles_load(&profiles, path, error, sizeof(error)) == 1 && profiles.count == 1);

    reset_faults(); fail_directory = 1;
    assert(config_profile_remove(path, id, &committed, error, sizeof(error)) == -1 && committed);
    reset_faults();
    assert(config_profiles_load(&profiles, path, error, sizeof(error)) == 1 && !profiles.count);
    assert_nick(profile_path, "Changed");
    assert(config_profile_remove(path, id, &committed, error, sizeof(error)) == 1 && committed);

    reset_faults(); fail_regular = 2;
    assert(config_profile_save(config, path, id, &committed, error, sizeof(error)) == -1 && !committed);
    reset_faults();
    assert(config_profiles_load(&profiles, path, error, sizeof(error)) == 1 && !profiles.count);
    assert(config_profile_save(config, path, id, &committed, error, sizeof(error)) == 1 && committed);
    assert(config_profiles_load(&profiles, path, error, sizeof(error)) == 1 && profiles.count == 1 && !strcmp(profiles.ids[0], id));
    inspect_tree(directory, 0);
    puts("storage faults: one real short-write limit and seven injected file-fsync/rename/directory-fsync failures; complete old/new files, accurate commit state, stable-ID retries, no leaked temporary files");
}

static void concurrent_profiles(const Config *original, const char *path)
{
    enum { WORKERS = 7, SAVES = 20 };
    int start[2], report[2];
    pid_t children[WORKERS];
    assert(!pipe(start) && !pipe(report));
    for (int worker = 0; worker < WORKERS; ++worker) {
        children[worker] = fork();
        assert(children[worker] >= 0);
        if (!children[worker]) {
            close(start[1]); close(report[0]);
            char byte, error[512], id[CONFIG_PROFILE_ID_SIZE];
            assert(read(start[0], &byte, 1) == 1); close(start[0]);
            Config config = *original;
            assert(snprintf(id, sizeof(id), "worker%d.ini", worker) > 0);
            assert(snprintf(config.server, sizeof(config.server), "worker%d.invalid", worker) > 0);
            unsigned busy = 0;
            for (int turn = 0; turn < SAVES; ++turn) {
                assert(snprintf(config.nick, sizeof(config.nick), "Worker%d_%02d", worker, turn) > 0);
                int committed, result;
                unsigned retries = 0;
                do {
                    result = config_profile_save(&config, path, id, &committed, error, sizeof(error));
                    if (result < 0) {
                        assert(strstr(error, "busy") && ++retries < 5000);
                        ++busy; pause_briefly(1000000);
                    }
                } while (result < 0);
                assert(committed);
            }
            assert(write(report[1], &busy, sizeof(busy)) == (ssize_t)sizeof(busy));
            close(report[1]); _exit(0);
        }
    }
    close(start[0]); close(report[1]);
    assert(write(start[1], "1234567", WORKERS) == WORKERS); close(start[1]);
    int pending = WORKERS;
    unsigned snapshots = 0;
    int64_t deadline = net_now_ms() + 15000;
    while (pending && net_now_ms() < deadline) {
        ConfigProfiles profiles;
        char error[512], profile[CONFIG_PATH_MAX];
        assert(config_profiles_load(&profiles, path, error, sizeof(error)) >= 0);
        assert(profiles.count <= WORKERS);
        for (size_t i = 0; i < profiles.count; ++i) {
            Config config;
            config_defaults(&config);
            assert(config_profile_path(profile, sizeof(profile), path, profiles.ids[i], error, sizeof(error)) == 1);
            assert(config_load(&config, profile, error, sizeof(error)) == 1 && !strncmp(config.server, "worker", 6));
        }
        ++snapshots;
        for (int i = 0; i < WORKERS; ++i) if (children[i]) {
            int status;
            pid_t done = waitpid(children[i], &status, WNOHANG);
            assert(done >= 0);
            if (done) {
                assert(WIFEXITED(status) && !WEXITSTATUS(status));
                children[i] = 0; --pending;
            }
        }
        pause_briefly(1000000);
    }
    if (pending) for (int i = 0; i < WORKERS; ++i) if (children[i]) {
        kill(children[i], SIGKILL); waitpid(children[i], NULL, 0);
    }
    assert(!pending);
    unsigned total_busy = 0;
    for (int i = 0; i < WORKERS; ++i) {
        unsigned busy;
        assert(read(report[0], &busy, sizeof(busy)) == (ssize_t)sizeof(busy));
        total_busy += busy;
    }
    close(report[0]);
    ConfigProfiles profiles;
    char error[512];
    assert(config_profiles_load(&profiles, path, error, sizeof(error)) == 1 && profiles.count == WORKERS);
    for (int i = 0; i < WORKERS; ++i) {
        char id[CONFIG_PROFILE_ID_SIZE], profile[CONFIG_PATH_MAX], nick[64];
        assert(snprintf(id, sizeof(id), "worker%d.ini", i) > 0 && snprintf(nick, sizeof(nick), "Worker%d_19", i) > 0);
        assert(config_profile_path(profile, sizeof(profile), path, id, error, sizeof(error)) == 1);
        assert_nick(profile, nick);
    }
    assert(config_profiles_check(path, error, sizeof(error)) == 1);
    printf("catalog contention: %d processes x %d saves; %u busy retries; %u concurrent complete snapshots; no lost entries\n",
           WORKERS, SAVES, total_busy, snapshots);
}

static void symlink_race(const Config *original, const char *directory)
{
    char path[CONFIG_PATH_MAX], victim[CONFIG_PATH_MAX], link[CONFIG_PATH_MAX], stage[CONFIG_PATH_MAX], error[512];
    assert(snprintf(path, sizeof(path), "%s/race.ini", directory) > 0);
    assert(snprintf(victim, sizeof(victim), "%s/victim.ini", directory) > 0);
    assert(snprintf(link, sizeof(link), "%s/swap-link", directory) > 0);
    assert(snprintf(stage, sizeof(stage), "%s/swap-file", directory) > 0);
    Config marked = *original;
    strcpy(marked.nick, "Victim");
    assert(config_save(&marked, victim, error, sizeof(error)) == 1);
    assert(config_save(original, path, error, sizeof(error)) == 1);
    char before[8192], after[8192];
    size_t size = read_file(victim, before, sizeof(before));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        for (int i = 0; i < 400; ++i) {
            assert(!symlink(victim, link) && !rename(link, path));
            pause_briefly(500000);
            assert(config_save(original, stage, error, sizeof(error)) == 1 && !rename(stage, path));
            pause_briefly(500000);
        }
        _exit(0);
    }
    unsigned refused = 0, writes = 0, reads = 0;
    for (int i = 0; i < 400; ++i) {
        int result = config_save(original, path, error, sizeof(error));
        if (result < 0) ++refused; else ++writes;
        Config loaded;
        config_defaults(&loaded);
        result = config_load(&loaded, path, error, sizeof(error));
        if (result > 0) { assert(!strcmp(loaded.nick, original->nick)); ++reads; }
        pause_briefly(1000000);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(refused && writes && reads);
    assert(read_file(victim, after, sizeof(after)) == size && !memcmp(before, after, size));
    printf("symlink race: 400 swaps and 400 save/load pairs; %u refused writes, %u complete writes, %u safe reads; target unchanged\n",
           refused, writes, reads);
}

static void directory_race(const Config *original, const char *directory)
{
    char live[CONFIG_PATH_MAX], parked[CONFIG_PATH_MAX], outside[CONFIG_PATH_MAX];
    char path[8192], victim[8192], error[512], before[8192], after[8192];
    assert(snprintf(live, sizeof(live), "%s/live-directory", directory) > 0);
    assert(snprintf(parked, sizeof(parked), "%s/parked-directory", directory) > 0);
    assert(snprintf(outside, sizeof(outside), "%s/outside-directory", directory) > 0);
    assert(snprintf(path, sizeof(path), "%s/config.ini", live) > 0);
    assert(snprintf(victim, sizeof(victim), "%s/config.ini", outside) > 0);
    Config marked = *original;
    strcpy(marked.nick, "Victim");
    assert(config_save(original, path, error, sizeof(error)) == 1);
    assert(config_save(&marked, victim, error, sizeof(error)) == 1);
    size_t size = read_file(victim, before, sizeof(before));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        for (int i = 0; i < 400; ++i) {
            assert(!rename(live, parked) && !symlink(outside, live));
            pause_briefly(500000);
            assert(!unlink(live) && !rename(parked, live));
            pause_briefly(500000);
        }
        _exit(0);
    }
    unsigned rejected = 0, reads = 0;
    for (int i = 0; i < 400; ++i) {
        Config loaded = *original;
        strcpy(loaded.nick, "Preserved");
        Config unchanged = loaded;
        int result = config_load(&loaded, path, error, sizeof(error));
        if (result > 0) { assert(!strcmp(loaded.nick, original->nick)); ++reads; }
        else { assert(!memcmp(&loaded, &unchanged, sizeof(loaded))); ++rejected; }
        pause_briefly(1000000);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(rejected && reads);
    assert(!rename(live, parked) && !symlink(outside, live));
    assert(config_save(original, path, error, sizeof(error)) == -1);
    assert(!unlink(live) && !rename(parked, live));
    assert(read_file(victim, after, sizeof(after)) == size && !memcmp(before, after, size));
    printf("directory race: 400 ancestor swaps, %u safe reads, %u refused/missing snapshots; unchanged caller on failure and no outside write\n",
           reads, rejected);
}

int main(void)
{
    char directory[] = "/tmp/vipirc-storage-stress-XXXXXX", primary[CONFIG_PATH_MAX], concurrent[CONFIG_PATH_MAX];
    assert(mkdtemp(directory));
    assert(snprintf(primary, sizeof(primary), "%s/config.ini", directory) > 0);
    assert(snprintf(concurrent, sizeof(concurrent), "%s/concurrent.ini", directory) > 0);
    Config config;
    config_defaults(&config);
    strcpy(config.server, "storage.invalid"); strcpy(config.nick, "Original");
    int before = descriptors();
    int64_t began = net_now_ms();
    app_validation_atomic(&config, primary);
    failure_atomicity(&config, primary, directory);
    concurrent_profiles(&config, concurrent);
    symlink_race(&config, directory);
    directory_race(&config, directory);
    inspect_tree(directory, 1);
    assert(descriptors() == before);
    errno = 0;
    assert(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    printf("storage stress: elapsed_ms=%lld fd_start=%d fd_end=%d children=0\n",
           (long long)(net_now_ms() - began), before, descriptors());
    return 0;
}
