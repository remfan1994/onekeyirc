/* Additional follow-up review probe. All paths/helpers are temporary. It uses
 * the existing adversarial executable fixture and production process code;
 * no GPG agent, real keyring, IRC connection or AF_UNIX socket is used. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "credentials.c"
#define main credential_suite_main
#include "test_credentials.c"
#undef main

static void still_running(Credential *job, pid_t pid)
{
    assert(!job->exited && children[job->child].pid == pid && !children[job->child].retired);
    assert(kill(pid, 0) == 0);
    int status;
    assert(waitpid(pid, &status, WNOHANG) == 0);
}

int main(int argc, char **argv)
{
    if (!strcmp(argv[0], "gpg")) return fake_gpg(argc, argv);
    struct rlimit no_core = {0, 0}; assert(setrlimit(RLIMIT_CORE, &no_core) == 0);
    char directory[] = "/tmp/onekey-gpg-lifecycle-XXXXXX";
    char executable[CONFIG_PATH_MAX], program[CONFIG_PATH_MAX], source[CONFIG_PATH_MAX];
    char destination[CONFIG_PATH_MAX], invalid[CONFIG_PATH_MAX], error[512], out[CONFIG_SECRET_MAX + 1];
    assert(realpath(argv[0], executable) && mkdtemp(directory));
    assert(snprintf(program, sizeof program, "%s/gpg", directory) > 0);
    assert(snprintf(source, sizeof source, "%s/input.gpg", directory) > 0);
    assert(snprintf(destination, sizeof destination, "%s/output.gpg", directory) > 0);
    assert(snprintf(invalid, sizeof invalid, "%s/missing/output.gpg", directory) > 0);
    assert(symlink(executable, program) == 0 && setenv("PATH", directory, 1) == 0);
    file_write(source, "okay", 4, 0600);

    for (unsigned mask = 0; mask < 8; ++mask) {
        pid_t pid = fork(); assert(pid >= 0);
        if (!pid) {
            for (int fd = 0; fd < 3; ++fd) if (mask & (1u << fd)) assert(close(fd) == 0);
            int before = descriptors();
            Credential *read_job = credential_start(source, milliseconds(), error, sizeof error);
            assert(read_job && finish(read_job, out, sizeof out, error, sizeof error) == 1);
            assert(!strcmp(out, "  secret #;=  "));
            config_wipe(out, sizeof out); credential_cancel(read_job); reap_all();
            CredentialCreate *write_job = credential_create_start(destination, fingerprint, "okay", milliseconds(), error, sizeof error);
            assert(write_job && finish_creation(write_job, error, sizeof error) == 1);
            assert(credential_create_publish(write_job, error, sizeof error) == 1);
            credential_create_cancel(write_job); reap_all();
            assert(access(destination, F_OK) < 0 && descriptors() == before);
            _exit(0);
        }
        int status; assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    /* Canceling a completed job must not cancel a newer helper reusing its
     * slot; a pre-fork creation error must also leave that helper untouched. */
    int before = descriptors();
    Credential *completed = credential_start(source, milliseconds(), error, sizeof error);
    assert(completed && finish(completed, out, sizeof out, error, sizeof error) == 1 && completed->exited);
    config_wipe(out, sizeof out);
    file_write(source, "hang", 4, 0600);
    Credential *active = credential_start(source, milliseconds(), error, sizeof error);
    assert(active && completed->child == active->child);
    pid_t active_pid = children[active->child].pid;
    credential_cancel(completed);
    still_running(active, active_pid);
    assert(!credential_create_start(invalid, fingerprint, "okay", milliseconds(), error, sizeof error));
    still_running(active, active_pid);
    credential_cancel(active); reap_all(); assert(descriptors() == before);

    CredentialCreate *ready = credential_create_start(destination, fingerprint, "okay", milliseconds(), error, sizeof error);
    assert(ready && finish_creation(ready, error, sizeof error) == 1 && ready->exited);
    assert(credential_create_publish(ready, error, sizeof error) == 1);
    active = credential_start(source, milliseconds(), error, sizeof error);
    assert(active && active->child == ready->child);
    active_pid = children[active->child].pid;
    credential_create_cancel(ready);
    assert(access(destination, F_OK) < 0);
    still_running(active, active_pid);
    credential_cancel(active); reap_all(); assert(descriptors() == before);
    assert(unlink(source) == 0 && unlink(program) == 0 && rmdir(directory) == 0);
    puts("credential lifecycle follow-up: all 8 closed-standard-fd combinations, decrypt/encrypt fd isolation, completed decrypt/encrypt slot reuse, unrelated pre-fork failure, rollback, child collection and fd cleanup passed");
    return 0;
}
