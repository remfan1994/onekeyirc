#ifndef VIPIRC_CLIENT_H
#define VIPIRC_CLIENT_H

#include "app.h"
#include <stdint.h>

#define CLIENT_MAX_SERVERS 8
#define CLIENT_SESSION_NOTICE "Cow killing is the bloodshed of the innocent. www.bloodguiltcurse.net - ban beef."

typedef struct Client Client;

typedef struct ClientSlot {
    int used, saved, load_failed;
    /* Runtime incarnation, never a displayed row or saved profile ID.
     * Deferred actions use it so a reused slot cannot inherit an old action. */
    uint64_t token;
    char profile_id[CONFIG_PROFILE_ID_SIZE];
    char config_path[CONFIG_PATH_MAX];
    char error[512];
    /* Unopened slots keep seed; opening creates the owned App lazily.
     * Frontends borrow app and must not free it during an interface switch. */
    Config seed;
    App *app;
    Client *owner;
    size_t index;
} ClientSlot;

struct Client {
    ClientSlot slots[CLIENT_MAX_SERVERS];
    size_t current;
    uint64_t next_token;
    int test_mode;
    int reconnect_enabled;
    int backgrounded;
    /* Local reminder, once per worker across servers, reconnects and frontends.
     * A detached worker leaves it pending until a terminal can show it. */
    int session_notice_shown;
    /* A failed saved default must not silently redirect an implicit connect. */
    int default_unavailable;
    char primary_path[CONFIG_PATH_MAX];
    char login[64];
    char catalog_error[512];
};

/* All integer results are 1 for success and -1 for failure. Startup/catalog
 * errors are retained on their rows; healthy profiles remain usable.
 * Slots never compact. Primary slot zero can disconnect but cannot close. */
int client_init(Client *, const Config *primary, const char *primary_path,
                const char *login, int test_mode, int primary_saved,
                char *error, size_t error_cap);
void client_free(Client *);
/* Runtime preference; applies to current and subsequently opened sessions.
 * Does not initiate a connection or modify saved autoconnect preferences. */
void client_set_reconnect(Client *, int enabled);
void client_user_activity(Client *);
void client_set_backgrounded(Client *, int backgrounded);
int client_request_quit(Client *, const char *reason);
App *client_get_app(Client *, size_t index, char *error, size_t error_cap);
void client_new_config(const Client *, Config *);
int client_add(Client *, const Config *, size_t *index, char *error, size_t error_cap);
int client_connect(Client *, size_t index, char *error, size_t error_cap);
void client_disconnect(Client *, size_t index);
int client_close(Client *, size_t index, char *error, size_t error_cap);
int client_save(Client *, size_t index, const Config *, char *error, size_t error_cap);
/* Save one effective profile and select its stable identity for next startup.
 * Profile and default-reference writes are separate; failures report partial saves. */
int client_save_default(Client *, size_t index, char *error, size_t error_cap);
/* Mouse, roster visibility, hints and shortcut labels are application preferences.
 * The primary file owns their saved values; Apply updates every live and not-yet-opened slot.
 * Saving is completed before any in-memory preference changes. */
int client_interface_apply(Client *, const Config *, int save, char *error, size_t error_cap);
int client_startup(Client *, int primary_connect, int no_connect, char *error, size_t error_cap);

#endif
