/* One IRC session's state and operations; Client owns the App allocation.
 * This core has no frontend event loop. Drive it through app_network and
 * the poll helpers, then present its retained state or display callbacks.
 * Declarations below group state ownership, lifecycle, views, I/O and sends.
 */
#ifndef VIPIRC_APP_H
#define VIPIRC_APP_H

#include "config.h"
#include "credentials.h"
#include "net.h"
#include "sim.h"
#include "protocol.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define VIPIRC_VERSION "0.7.36"
#define ONEKEY_VERSION "0.1.35"
#define APP_BUFFERS 32
#define APP_USERS 4096
#define APP_QUEUE 64
#define APP_LOG_BYTES (8u * 1024u * 1024u)
#define APP_PERSISTENT_LOG_MAX (16u * 1024u * 1024u)
#define APP_TEXT 16384
#define APP_HELP_LINES 2048
#define APP_LIST_MAX 8192u
#define APP_LIST_TOPIC 512u
#define APP_BATCHES 16u
#define APP_BATCH_REF 64u

typedef enum AppAutomaticAway {
    APP_AUTO_AWAY_NONE = 0,
    APP_AUTO_AWAY_IDLE = 1,
    APP_AUTO_AWAY_BACKGROUND = 2
} AppAutomaticAway;

typedef enum BufferKind {
    B_SERVER, B_MENTIONS, B_HELP, B_CHANNEL, B_QUERY
} BufferKind;
typedef struct User {
    char nick[64];
    unsigned modes;
} User;
typedef enum AppListSort {
    APP_LIST_SORT_CHANNEL, APP_LIST_SORT_USERS, APP_LIST_SORT_TOPIC
} AppListSort;
typedef struct ChannelListEntry {
    char channel[201];
    uint64_t users;
    char topic[APP_LIST_TOPIC];
} ChannelListEntry;
/* Entries are owned by this cache. Revision pairs invalidate lazy sorting;
 * active belongs to a live request, while complete describes its snapshot. */
typedef struct ChannelList {
    ChannelListEntry *entries;
    size_t count, capacity, received, matched, display_limit;
    uint64_t revision, sorted_revision;
    uint64_t min_users, max_users;
    int min_set, max_set, sort, descending, active, complete, truncated;
    char server_mask[201], name_filter[201], topic_filter[257];
} ChannelList;
/* App owns the line ring and roster allocations. Buffer indexes can move on
 * view closure; id is the stable identity used by retained mention records. */
typedef struct Buffer {
    char name[201];
    BufferKind kind;
    int joined, background_join, unread, unread_mentions, names_truncated, names_in_progress;
    char **lines;
    uint64_t id;
    uint64_t *line_origins; /* Mentions only: stable source-buffer IDs. */
    char (*line_queries)[64]; /* Lazy closed-query names, indexed with origins. */
    size_t line_query_count;
    size_t line_cap, line_start, line_count, scroll;
    /* Catch-up anchors are runtime-only. last_own_from_end identifies the
     * newest locally sent chat/action while it remains in bounded memory.
     * The log anchor binds that record's offset to its file and full-record
     * checksum, so rotation or rewriting cannot reuse a stale byte position.
     * It survives memory eviction while this process runs. */
    size_t last_own_from_end;
    int last_own_retained, last_own_log_valid;
    int64_t last_own_log_offset;
    dev_t last_own_log_device;
    ino_t last_own_log_inode;
    size_t last_own_log_length;
    uint64_t last_own_log_checksum;
    User *users;
    size_t user_count, user_cap, user_scroll;
} Buffer;
typedef struct OutLine OutLine;
typedef struct AppBatch {
    char reference[APP_BATCH_REF + 1u];
    unsigned parent; /* Parent slot + 1, or zero. Parents outlive children. */
    int history;
} AppBatch;
typedef enum AppStatusKind {
    APP_STATUS_GENERAL, APP_STATUS_QUEUE_NOTICE, APP_STATUS_PROGRAM_NOTICE
} AppStatusKind;
/* Return 1 on success or -1 on failure, matching config_save. */
typedef int (*AppSaveConfig)(void *, const Config *, char *, size_t);
/* An offer callback records a direct user DCC request without accepting it.
 * The CTCP body has no delimiters; return 1 when handled, 0 otherwise. */
typedef int (*AppDccOffer)(void *, const char *nick, const char *ctcp_body);
typedef struct App {
    /* Effective live settings plus edits reserved for the next connection. */
    Config cfg;
    Config config_pending;
    unsigned config_pending_mask;
    char config_path[CONFIG_PATH_MAX];
    /* Automatic usage persistence starts only after a configuration was
     * loaded or successfully saved; an intended default path is not enough. */
    int config_saved;

    /* Owned transport/helper state. Test mode uses sim instead of net. */
    Net *net;
    Sim *sim;
    Credential *credential;
    int credential_kind;
    int test_mode, visible;
    char test_channels[1024];

    /* Canonical conversation state, independent of a frontend's screen. */
    Buffer buffers[APP_BUFFERS];
    size_t buffer_count, current, log_bytes;
    ChannelList list;
    IrcCase casemap;
    char nick[64], own_user[64], own_host[256];
    char prefix_modes[17], prefix_chars[17], chanmodes[128];

    /* Input framing and per-connection registration/replay state. */
    char incoming[IRC_WIRE_MAX + 1];
    size_t incoming_len, wire_limit;
    int registered, started, cap_done, cap_sasl, cap_time, cap_multi, away;
    int cap_batch, cap_self;
    unsigned cap_offered, cap_pending;
    AppBatch batches[APP_BATCHES];
    /* Synchronous incoming-message display context; never used for output. */
    char message_time[32];
    int message_history;
    int automatic_away, manual_away_pending, backgrounded;
    /* A local return may need to retry clearing automatic away after a full
     * control queue. The latest accepted presence intent owns this request. */
    int automatic_away_clear_pending;
    int64_t last_local_activity;
    int64_t automatic_away_retry_at;
    int auth_started, auth_sent, authenticated, bad_lines, dirty, quit;
    int64_t began, last_receive, last_chat, last_ping;
    /* Runtime-only opt-in. Explicit disconnect and fatal failures cancel a
     * pending retry; automatic attempts retain the last connection settings. */
    int reconnect_enabled;
    unsigned reconnect_attempts;
    int64_t reconnect_at, registered_at;
    uint64_t connection_id;
    uint64_t next_buffer_id;

    /* Queued nodes and the detached frame currently owned by a write retry. */
    OutLine *control_head, *control_tail, *chat_head, *chat_tail, *inflight;
    size_t control_count, chat_count;
    /* One automatic command may be queued/in flight. Config is the immutable
     * per-connection snapshot; config_pending is never executed here. */
    size_t autocommand_next;
    int autocommand_active, autocommand_waiting;
    /* Only the current unsent script AWAY/BACK may restore prior presence
     * bookkeeping; newer presence intent or a write attempt supersedes it. */
    OutLine *autocommand_presence;
    int64_t autocommand_due;
    char status[512];
    AppStatusKind status_kind;
    int persistent_log_failed;
    int persistent_log_suppressed;
    void (*view_closed)(void *context, size_t index);
    void *view_context;
    AppSaveConfig save_config;
    void *save_context;
    AppDccOffer dcc_offer;
    void *dcc_context;
    /* Optional semantic output for the non-curses frontend. Callbacks receive
     * sanitized display records synchronously; they must not reenter App.
     * A mentions copy has B_MENTIONS kind and is not a second wire message.
     * stream_only retains memberships/activity but delegates history storage.
     * The final callback flag marks the first displayed IRC line of the most
     * recent locally sent composition for bounded catch-up anchoring. */
    void (*line_event)(void *, const struct App *, size_t, const char *, int, int);
    void (*status_event)(void *, const struct App *, const char *);
    void *event_context;
    int stream_only;
} App;
/* Caller-owned snapshot made by app_plan; no borrowed pointers. Its
 * connection_id is separate from ClientSlot.token, which the frontend uses
 * to protect against server-slot reuse while review is open. */
typedef struct SendPlan {
    char target[201], text[APP_TEXT + 1];
    IrcChunk chunks[APP_QUEUE];
    size_t count, budget;
    uint64_t connection_id;
    int action;
} SendPlan;

/* Lifecycle/configuration. app_init returns 1 on success, 0 on failure;
 * app_free releases owned resources and wipes, but does not free, the App. */
int app_init(App *, const Config *, const char *path);
/* Effective settings include staged edits for the next explicit connection. */
void app_config_effective(const App *, Config *);
/* Validate and atomically apply live preferences. Save succeeds before mutation.
 * Connection edits are staged until the next disconnected Connect. Return 1/0. */
int app_config_apply(App *, const Config *, int save, char *error, size_t error_cap);
void app_free(App *);

/* Presentation records. Format strings are trusted local code; remote text
 * belongs in arguments and is sanitized before display/storage. */
void app_log(App *, size_t view, const char *format, ...);
/* Return the open private log's snapshot end only if its saved sent record
 * still matches; otherwise -1. The checksum detects local accidental edits,
 * not malicious changes by the account owner. Does not close fd. */
int64_t app_catchup_log_end(const Buffer *, int fd);
void app_status(App *, const char *format, ...);
void app_program_notice(App *, const char *format, ...);
void app_queue_notice(App *, const char *format, ...);
const char *app_status_text(const App *);
/* Structured IRC LIST cache shared by curses and minimalist frontends.
 * Filtering is local for portability; server_mask is the only value sent to IRC. */
void app_list_clear(App *);
void app_list_sort(App *, int sort, int descending);
size_t app_list_visible_count(const App *);
int app_list_repeat(App *);
/* Cancel remaining automatic commands and wipe queued bytes. An already
 * in-flight write must finish unchanged, including TLS retry buffers. */
size_t app_autocommands_cancel(App *);

/* Views and activity. app_buffer returns SIZE_MAX when absent/refused; it
 * creates only when requested. Borrowed view/line pointers must not survive
 * an operation which closes, clears, compacts or reallocates that state. */
size_t app_buffer(App *, const char *, BufferKind, int create);
void app_switch(App *, size_t);
/* Only the main view that is actually displayed is visible. Hidden sessions
 * and covered main views accumulate activity, including their current view.
 * Revealing a main view marks that view's unread and mention counts read. */
void app_set_visible(App *, int visible);
/* Source-view totals exclude the copied mentions feed and the manual.
 * Counters saturate rather than wrap; either output pointer may be NULL. */
void app_activity(const App *, size_t *unread, size_t *mentions);
/* Resolve a retained mention's source after renames/compaction. SIZE_MAX means
 * the source was closed or the logical mention index is no longer retained. */
size_t app_mention_view(const App *, size_t logical_index);
/* Retained source identity does not change when another buffer takes its slot. */
uint64_t app_mention_origin(const App *, size_t logical_index);
/* Open a retained source, recreating only explicitly closed queries locally.
 * This never sends, joins, or reconnects. SIZE_MAX reports an unavailable source. */
size_t app_open_mention(App *, uint64_t origin);
/* Remove copied mention records only; source history/counters/queues stay intact. */
size_t app_clear_mentions(App *);
/* Cycle main views, excluding the reserved secondary manual buffer. */
void app_cycle(App *, int direction);
/* Close a dynamic view. Callback runs once with old indexes before compaction. */
int app_close_view(App *, size_t);

/* Connection lifecycle and event-loop integration. */
/* Enable permanently offline mode before any connection; no configuration write. */
int app_set_test_mode(App *);
int64_t app_now_ms(const App *);
void app_set_reconnect(App *, int enabled);
void app_user_activity(App *);
void app_set_backgrounded(App *, int backgrounded);
int app_request_quit(App *, const char *reason);
int app_connect(App *);
void app_disconnect(App *, const char *reason);
/* Run on readiness and timeout-only turns, including while disconnected;
 * reconnect deadlines, credential cleanup and presence timers need progress. */
void app_network(App *, short revents);
/* Poll descriptor, or -1 while offline, disconnected or waiting for the
 * server PASS credential. Credential work still advances on timer turns. */
int app_fd(const App *);
/* Recompute descriptor/events/deadline each poll turn; progress can replace
 * the resolver descriptor with a socket or close the connection entirely. */
short app_events(const App *);
/* Milliseconds to next useful work, capped at one second; 0 means due now. */
int app_timeout(const App *);

/* Commands and composition. */
/* 0=error, 1=handled, 2=send plan, 3=channels, 4=queries, 5=manual, 6=network channel directory. */
int app_command(App *, const char *text, SendPlan *plan);
typedef struct AppDraftMeasure {
    char target[201];
    size_t budget, payload_bytes, longest_line_bytes, messages;
    int action, fits;
} AppDraftMeasure;
/* Pure composition preview: no commands, statuses, views or network effects.
 * Return 1 for a recognized valid chat draft (including empty text), else 0.
 * LF separators do not count as payload; blank logical lines are skipped.
 * messages is exact from 0 through APP_QUEUE; APP_QUEUE+1 means more than 64.
 * fits describes splitter capacity, not registration/join/send permission.
 * A zero budget or a scalar larger than the budget gives fits=0,messages=0. */
int app_measure_draft(const App *, size_t view, const char *utf8, int literal,
                      AppDraftMeasure *);
/* Prepare without queueing; failures may set status. The frontend obtains
 * any required review/consent before app_send_plan revalidates and queues. */
int app_plan(App *, const char *target, const char *text, int action, SendPlan *);
/* Pure history privacy check for a prepared message. Recompute exact chunks;
 * return 0 for an invalid plan or recognized private credential content. This
 * supplements the frontend's command syntax filter and has no send effects. */
int app_plan_history_safe(const SendPlan *);
/* 1 queues the entire plan, 0 queues none. This is not delivery confirmation;
 * actual writes and local echo happen later through app_network. */
int app_send_plan(App *, const SendPlan *);
size_t app_budget(const App *, const char *target, int action);
void app_clear_buffer(App *, size_t);
/* Populate the secondary manual without changing the current main view. */
void app_help(App *);
const char *app_connection_label(const App *);
int app_outgoing(App *, int chat, const char *format, ...);
/* Queue a paced DCC SEND offer associated with its stable transfer id. */
int app_queue_dcc_offer(App *, uint64_t id, const char *nick, const char *body);
/* Remove a queued offer. 1=removed, 0=not queued, 2=already in flight.
 * An in-flight frame is preserved, including a TLS write awaiting retry. */
int app_cancel_dcc_offer(App *, uint64_t id);

#endif
