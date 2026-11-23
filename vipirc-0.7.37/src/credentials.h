#ifndef VIPIRC_CREDENTIALS_H
#define VIPIRC_CREDENTIALS_H

#include <stddef.h>
#include <stdint.h>

#define CREDENTIAL_TIMEOUT_MS 5000
typedef struct Credential Credential;
typedef struct CredentialCreate CredentialCreate;

#define CREDENTIAL_FINGERPRINT_MAX 64
#define CREDENTIAL_CIPHERTEXT_MAX 65536

/* Drive these APIs from the owning event loop with monotonic milliseconds.
 * The module has one process-wide child table; its API is not thread-safe.
 * Polling never transfers ownership of a job; release it explicitly below.
 */

/* Noninteractive GnuPG job. All arguments are paths or local constants;
 * plaintext is returned only after successful integrity-checked decryption.
 * The caller owns the returned job until credential_cancel, including after
 * a final poll result, and must wipe any successful plaintext copy. Child
 * ownership is private: do not reap helper children or change SIGCHLD policy. */
Credential *credential_start(const char *path, int64_t now_ms, char *error, size_t error_cap);
/* 0=pending, 1=one validated secret copied to out, -1=failure. Never waits. */
int credential_poll(Credential *, int64_t now_ms, char *out, size_t out_cap,
                    char *error, size_t error_cap);
/* Kill, close pipes and wipe immediately. Child collection never blocks. */
void credential_cancel(Credential *);
/* Continue reaping on later event-loop turns after cancellation; pending
 * cleanup is also a barrier to the first background fork. */
void credential_reap(void);
int credential_cleanup_pending(void);

/* Create a new encrypted credential using an already installed local public
 * key. Only a full 40/64-digit hexadecimal fingerprint is accepted. The secret
 * stays in bounded memory/pipes; the destination is untouched until publish. */
int credential_fingerprint_valid(const char *);
/* Resolve a literal destination against the current directory without following
 * symlinks or expanding shell syntax. Reject .. and control bytes. 1/-1. */
int credential_absolute_path(const char *path, char *out, size_t out_cap,
                             char *error, size_t error_cap);
/* Read-only early check: owned private directory, absent destination and local
 * gpg executable. Does not create files/directories or start a helper. 1/-1. */
int credential_create_check(const char *path, char *error, size_t error_cap);
/* Read-only suggestion beside config_path in credentials/account-password.gpg
 * or server-password.gpg; collisions use -2 through -64. Always absolute. */
int credential_default_path(const char *config_path, int server_password,
                            char *out, size_t out_cap, char *error, size_t error_cap);
/* Only after explicit acceptance of a managed default: create missing parent
 * directories as owned 0700 directories. Never chmod existing directories,
 * follow symlinks or replace files. Verify the exact managed destination. */
int credential_default_prepare(const char *config_path, const char *destination,
                               char *error, size_t error_cap);
/* Allocate a caller-owned job or return NULL with a diagnostic. A final
 * poll result still requires finish/cancel; start does not publish a file. */
CredentialCreate *credential_create_start(const char *path, const char *fingerprint,
                                           const char *secret, int64_t now_ms,
                                           char *error, size_t error_cap);
/* 0=pending, 1=complete encryption ready to publish, -1=failure. Never waits. */
int credential_create_poll(CredentialCreate *, int64_t now_ms,
                            char *error, size_t error_cap);
/* Publish completed ciphertext as a new 0600 file in its owned private
 * directory. Never replaces an existing path. A successful repeat is safe. */
int credential_create_publish(CredentialCreate *, char *error, size_t error_cap);
/* True once this job linked its encrypted file, even if directory fsync then
 * failed. Read after every publish attempt before reporting saved locations. */
int credential_create_published(const CredentialCreate *);
/* Finish keeps a published file. Cancel removes only unchanged files created
 * by this job. Both stop/reap the helper, close descriptors and wipe memory. */
void credential_create_finish(CredentialCreate *);
void credential_create_cancel(CredentialCreate *);

#endif
