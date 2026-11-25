/* Shared hotkey grammar, bounded mutation and private config persistence.
 * This test never opens a terminal, a network socket or a credential file. */
#define _POSIX_C_SOURCE 200809L
#include "config.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_text(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    size_t n = strlen(text);
    assert(fd >= 0 && write(fd, text, n) == (ssize_t)n);
    assert(!fchmod(fd, 0600) && !close(fd));
}

static void key_grammar(void)
{
    static const struct { const char *name; unsigned key; } named[] = {
        {"Up", CONFIG_KEY_UP}, {"Down", CONFIG_KEY_DOWN},
        {"Left", CONFIG_KEY_LEFT}, {"Right", CONFIG_KEY_RIGHT},
        {"Home", CONFIG_KEY_HOME}, {"End", CONFIG_KEY_END},
        {"PageUp", CONFIG_KEY_PAGE_UP}, {"PageDown", CONFIG_KEY_PAGE_DOWN},
        {"Insert", CONFIG_KEY_INSERT}, {"Delete", CONFIG_KEY_DELETE},
        {"Backspace", CONFIG_KEY_BACKSPACE}, {"Tab", CONFIG_KEY_TAB},
        {"Shift-Tab", CONFIG_KEY_SHIFT_TAB}, {"Enter", CONFIG_KEY_ENTER},
        {"Escape", CONFIG_KEY_ESCAPE}
    };
    static const char *const invalid[] = {
        "", "F0", "F13", "F999999999999999999999", "Ctrl-", "Ctrl-AA",
        "Alt-", "Alt-aa", "Ctrl-Alt-a", "Shift-F1", "Mouse1", "a",
        "1", " ", " F1", "F1 ", "F1\n", "F1\r", "\033OP",
        "\t", "\177", "\xc3\xa9", "Alt-\xc3\xa9", "F1;quit", "F1=quit"
    };
    char name[32];
    for (size_t i = 0; i < sizeof named / sizeof named[0]; ++i) {
        assert(config_hotkey_key(named[i].name) == named[i].key);
        assert(config_hotkey_key_name(named[i].key, name, sizeof name));
        assert(config_hotkey_key(name) == named[i].key);
    }
    for (unsigned i = 0; i < 12; ++i) {
        assert(snprintf(name, sizeof name, "F%u", i + 1) > 0);
        assert(config_hotkey_key(name) == CONFIG_KEY_F1 + i);
        assert(config_hotkey_key_name(CONFIG_KEY_F1 + i, name, sizeof name));
        assert(config_hotkey_key(name) == CONFIG_KEY_F1 + i);
    }
    for (unsigned i = 1; i <= 26; ++i) {
        unsigned expected = i;
        if (i == 8) expected = CONFIG_KEY_BACKSPACE;
        if (i == 9) expected = CONFIG_KEY_TAB;
        if (i == 10 || i == 13) expected = CONFIG_KEY_ENTER;
        assert(snprintf(name, sizeof name, "Ctrl-%c", (int)('A' + i - 1)) > 0);
        assert(config_hotkey_key(name) == expected);
        assert(config_hotkey_key_name(expected, name, sizeof name));
        assert(config_hotkey_key(name) == expected);
    }
    for (unsigned i = 'a'; i <= 'z'; ++i) {
        assert(snprintf(name, sizeof name, "Alt-%c", (int)i) > 0);
        assert(config_hotkey_key(name) == CONFIG_KEY_ALT + i);
        assert(config_hotkey_key_name(CONFIG_KEY_ALT + i, name, sizeof name));
        assert(config_hotkey_key(name) == CONFIG_KEY_ALT + i);
    }
    for (unsigned i = '0'; i <= '9'; ++i) {
        assert(snprintf(name, sizeof name, "Alt-%c", (int)i) > 0);
        assert(config_hotkey_key(name) == CONFIG_KEY_ALT + i);
    }
    assert(config_hotkey_key("f7") == CONFIG_KEY_F1 + 6u);
    assert(config_hotkey_key("ctrl-a") == 1u);
    assert(config_hotkey_key("ALT-X") == CONFIG_KEY_ALT + 'x');
    assert(config_hotkey_key("Ctrl-[") == CONFIG_KEY_ESCAPE);
    assert(config_hotkey_key("Esc") == CONFIG_KEY_ESCAPE);
    assert(config_hotkey_key("Return") == CONFIG_KEY_ENTER);
    assert(config_hotkey_key("PgUp") == CONFIG_KEY_PAGE_UP);
    assert(config_hotkey_key("PgDn") == CONFIG_KEY_PAGE_DOWN);
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
        assert(!config_hotkey_key(invalid[i]));
    assert(!config_hotkey_key(NULL));
    assert(!config_hotkey_key_name(0, name, sizeof name));
    assert(!config_hotkey_key_name(UINT_MAX, name, sizeof name));
    assert(!config_hotkey_key_name(CONFIG_KEY_F1 + 12u, name, sizeof name));
    assert(!config_hotkey_key_name(CONFIG_KEY_ALT + '[', name, sizeof name));
    assert(!config_hotkey_key_name(CONFIG_KEY_F1, name, 1));
    puts("hotkey grammar: function/control/navigation/Alt roundtrips, terminal aliases and malformed input passed");
}

static void action_grammar(void)
{
    static const char *const required[] = {
        "compose", "command", "bindings", "help", "manual", "summary",
        "servers", "channels", "queries", "unread", "mentions", "transfers",
        "settings", "quickstart", "next", "prev", "clearinput", "cancel",
        "background", "quit"
    };
    static const char *const forbidden[] = {
        "", "send", "submit", "confirm", "/quit", "/msg someone secret",
        "quit now", "quit\n", "summary;quit", "$(command)", "`command`"
    };
    assert(config_hotkey_action_count() == sizeof required / sizeof required[0]);
    for (size_t i = 0; i < sizeof required / sizeof required[0]; ++i) {
        const ConfigHotkeyAction *a = config_hotkey_action_find(required[i]);
        assert(a && a->id != CONFIG_ACTION_NONE && !strcmp(a->name, required[i]));
        assert(config_hotkey_action(a->id) == a);
    }
    for (size_t i = 0; i < config_hotkey_action_count(); ++i) {
        const ConfigHotkeyAction *a = config_hotkey_action_at(i);
        assert(a && a->name && a->description && *a->description);
        assert(config_hotkey_action_find(a->name) == a);
        for (size_t j = 0; j < i; ++j) {
            const ConfigHotkeyAction *b = config_hotkey_action_at(j);
            assert(a->id != b->id && strcmp(a->name, b->name));
        }
        if (a->command) {
            assert(a->command[0] == '/');
            for (const char *p = a->command; *p; ++p)
                assert((unsigned char)*p >= 32 && (unsigned char)*p < 127);
        }
    }
    for (size_t i = 0; i < sizeof forbidden / sizeof forbidden[0]; ++i)
        assert(!config_hotkey_action_find(forbidden[i]));
    assert(!config_hotkey_action_find(NULL));
    assert(config_hotkey_action_find("SUMMARY") == config_hotkey_action(CONFIG_ACTION_SUMMARY));
    assert(!config_hotkey_action(CONFIG_ACTION_NONE));
    assert(!config_hotkey_action(-1) && !config_hotkey_action(INT_MAX));
    assert(!config_hotkey_action_at(config_hotkey_action_count()));
    assert(!config_hotkey_action_at((size_t)-1));
    puts("hotkey actions: complete fixed allowlist, no macro/send/confirmation actions passed");
}

static size_t candidate_keys(unsigned *keys, size_t capacity)
{
    size_t count = 0;
    char name[32];
    for (unsigned i = 1; i <= 26; ++i) {
        assert(snprintf(name, sizeof name, "Ctrl-%c", (int)('A' + i - 1)) > 0);
        unsigned key = config_hotkey_key(name);
        size_t j;
        for (j = 0; j < count && keys[j] != key; ++j) {}
        if (j == count) { assert(count < capacity); keys[count++] = key; }
    }
    for (unsigned i = 0; i < 12; ++i) { assert(count < capacity); keys[count++] = CONFIG_KEY_F1 + i; }
    for (unsigned i = 'a'; i <= 'z'; ++i) { assert(count < capacity); keys[count++] = CONFIG_KEY_ALT + i; }
    for (unsigned i = '0'; i <= '9'; ++i) { assert(count < capacity); keys[count++] = CONFIG_KEY_ALT + i; }
    return count;
}

static void bounded_mutation(void)
{
    Config c, before;
    unsigned keys[96];
    char error[256];
    size_t available = candidate_keys(keys, sizeof keys / sizeof keys[0]);
    assert(available > CONFIG_HOTKEY_MAX);
    config_defaults(&c);
    assert(c.hotkey_count == 0 && !c.use_signals);
    assert(config_hotkey_lookup(&c, CONFIG_KEY_F1) == CONFIG_ACTION_NONE);
    assert(config_hotkey_lookup(NULL, CONFIG_KEY_F1) == CONFIG_ACTION_NONE);
    assert(config_hotkey_set(&c, CONFIG_KEY_F1, CONFIG_ACTION_SUMMARY, error, sizeof error) == 1);
    assert(c.hotkey_count == 1 && !c.use_signals);
    assert(config_hotkey_lookup(&c, CONFIG_KEY_F1) == CONFIG_ACTION_SUMMARY);
    assert(config_hotkey_set(&c, CONFIG_KEY_F1, CONFIG_ACTION_HELP, error, sizeof error) == 1);
    assert(c.hotkey_count == 1 && config_hotkey_lookup(&c, CONFIG_KEY_F1) == CONFIG_ACTION_HELP);
    before = c;
    assert(config_hotkey_set(&c, UINT_MAX, CONFIG_ACTION_HELP, error, sizeof error) == -1);
    assert(!memcmp(&c, &before, sizeof c));
    assert(config_hotkey_set(&c, CONFIG_KEY_F1, INT_MAX, error, sizeof error) == -1);
    assert(!memcmp(&c, &before, sizeof c));
    assert(config_hotkey_set(&c, CONFIG_KEY_F1, CONFIG_ACTION_NONE, error, sizeof error) == 1);
    assert(!c.hotkey_count && config_hotkey_lookup(&c, CONFIG_KEY_F1) == CONFIG_ACTION_NONE);
    for (size_t i = 0; i < CONFIG_HOTKEY_MAX; ++i)
        assert(config_hotkey_set(&c, keys[i], CONFIG_ACTION_SUMMARY, error, sizeof error) == 1);
    assert(c.hotkey_count == CONFIG_HOTKEY_MAX && config_validate(&c, error, sizeof error) == 1);
    before = c;
    assert(config_hotkey_set(&c, keys[CONFIG_HOTKEY_MAX], CONFIG_ACTION_HELP, error, sizeof error) == -1);
    assert(!memcmp(&c, &before, sizeof c));
    assert(config_hotkey_set(&c, keys[0], CONFIG_ACTION_HELP, error, sizeof error) == 1);
    assert(c.hotkey_count == CONFIG_HOTKEY_MAX && config_hotkey_lookup(&c, keys[0]) == CONFIG_ACTION_HELP);
    assert(config_hotkey_set(&c, keys[0], CONFIG_ACTION_NONE, error, sizeof error) == 1);
    assert(c.hotkey_count == CONFIG_HOTKEY_MAX - 1u);
    for (size_t i = 1; i < CONFIG_HOTKEY_MAX; ++i)
        assert(config_hotkey_lookup(&c, keys[i]) == CONFIG_ACTION_SUMMARY);
    assert(config_hotkey_set(&c, keys[CONFIG_HOTKEY_MAX], CONFIG_ACTION_HELP, error, sizeof error) == 1);
    before = c;
    c.hotkey_count = CONFIG_HOTKEY_MAX + 1u;
    assert(config_validate(&c, error, sizeof error) == -1);
    Config broken = c;
    assert(config_hotkey_set(&c, CONFIG_KEY_F1, CONFIG_ACTION_HELP, error, sizeof error) == -1);
    assert(!memcmp(&c, &broken, sizeof c));
    assert(config_hotkey_lookup(&c, CONFIG_KEY_F1) == CONFIG_ACTION_NONE);
    c = before; c.hotkeys[0].key = UINT_MAX;
    assert(config_validate(&c, error, sizeof error) == -1);
    c = before; c.hotkeys[0].action = CONFIG_ACTION_NONE;
    assert(config_validate(&c, error, sizeof error) == -1);
    c = before; c.hotkeys[0].action = INT_MAX;
    assert(config_validate(&c, error, sizeof error) == -1);
    c = before; c.hotkeys[1].key = c.hotkeys[0].key;
    assert(config_validate(&c, error, sizeof error) == -1);
    puts("hotkey mutation: empty defaults, replace/remove, capacity, corruption checks and failed-mutation atomicity passed");
}

static void config_persistence(const char *path)
{
    static const char *const invalid[] = {
        "[hotkeys]\nF1=summary\nF1=quit\n",
        "[hotkeys]\nF1=summary\nf1=quit\n",
        "[hotkeys]\nCtrl-I=summary\nTab=quit\n",
        "[hotkeys]\nCtrl-M=summary\nEnter=quit\n",
        "[hotkeys]\nCtrl-J=summary\nCtrl-M=quit\n",
        "[hotkeys]\nCtrl-H=summary\nBackspace=quit\n",
        "[hotkeys]\nCtrl-[=summary\nEscape=quit\n",
        "[hotkeys]\nAlt-x=summary\nAlt-X=quit\n",
        "[hotkeys]\nF1=summary\n[hotkeys]\nF1=quit\n",
        "[hotkeys]\nF1=summary\nF13=quit\n",
        "[hotkeys]\nF1=summary\nF2=send\n",
        "[hotkeys]\nF1=summary\nF2=/quit\n",
        "[hotkeys]\nF1=summary\nF2=summary;quit\n",
        "[hotkeys]\nF1=summary\nF2=\n",
        "[hotkeys]\nF1=summary\ncommand=quit\n",
        "[hotkeys]\nF1=summary\nAlt-aa=quit\n"
    };
    unsigned keys[96];
    size_t count = candidate_keys(keys, sizeof keys / sizeof keys[0]);
    char error[512], text[8192], key[32];
    Config c, loaded, before;
    struct stat st;
    assert(count > CONFIG_HOTKEY_MAX);
    for (int minimal = 0; minimal < 2; ++minimal) {
        if (minimal) config_minimal_defaults(&c); else config_defaults(&c);
        assert(!c.hotkey_count);
        for (size_t i = 0; i < CONFIG_HOTKEY_MAX; ++i)
            assert(config_hotkey_set(&c, keys[i], config_hotkey_action_at(i % config_hotkey_action_count())->id,
                                     error, sizeof error) == 1);
        assert(config_save(&c, path, error, sizeof error) == 1);
        assert(!stat(path, &st) && (st.st_mode & 0777) == 0600);
        if (minimal) config_minimal_defaults(&loaded); else config_defaults(&loaded);
        assert(config_load(&loaded, path, error, sizeof error) == 1);
        assert(loaded.hotkey_count == c.hotkey_count && !loaded.use_signals);
        for (size_t i = 0; i < c.hotkey_count; ++i)
            assert(config_hotkey_lookup(&loaded, c.hotkeys[i].key) == c.hotkeys[i].action);
        for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
            before = loaded;
            write_text(path, invalid[i]);
            assert(config_load(&loaded, path, error, sizeof error) == -1);
            assert(!memcmp(&loaded, &before, sizeof loaded));
        }
        size_t used = (size_t)snprintf(text, sizeof text, "[hotkeys]\n");
        for (size_t i = 0; i <= CONFIG_HOTKEY_MAX; ++i) {
            assert(config_hotkey_key_name(keys[i], key, sizeof key));
            int n = snprintf(text + used, sizeof text - used, "%s=summary\n", key);
            assert(n > 0 && (size_t)n < sizeof text - used); used += (size_t)n;
        }
        before = loaded;
        write_text(path, text);
        assert(config_load(&loaded, path, error, sizeof error) == -1);
        assert(!memcmp(&loaded, &before, sizeof loaded));
        write_text(path, "[input]\nsend_mode=review\n");
        assert(config_load(&loaded, path, error, sizeof error) == 1);
        assert(loaded.hotkey_count == 0 && loaded.send_review);
        assert(config_hotkey_lookup(&loaded, keys[0]) == CONFIG_ACTION_NONE);
        assert(config_hotkey_set(&loaded, CONFIG_KEY_F1, CONFIG_ACTION_SUMMARY, error, sizeof error) == 1);
        write_text(path, "[hotkeys]\n");
        assert(config_load(&loaded, path, error, sizeof error) == 1 && loaded.hotkey_count == 0);
        assert(config_save(&loaded, path, error, sizeof error) == 1);
        assert(config_load(&c, path, error, sizeof error) == 1 && c.hotkey_count == 0);
        assert(config_hotkey_set(&c, 3, CONFIG_ACTION_SUMMARY, error, sizeof error) == 1);
        assert(config_hotkey_set(&c, 26, CONFIG_ACTION_HELP, error, sizeof error) == 1);
        assert(!c.use_signals);
        before = c; assert(!unlink(path));
        assert(config_load(&c, path, error, sizeof error) == 0);
        assert(!memcmp(&before, &c, sizeof c));
    }
    puts("hotkey persistence: both config formats, private roundtrip, duplicate aliases, 65-entry rejection, empty reload and signal preference isolation passed");
}

int main(void)
{
    char directory[] = "/tmp/irc-hotkeys-XXXXXX", path[CONFIG_PATH_MAX];
    assert(mkdtemp(directory));
    assert(snprintf(path, sizeof path, "%s/config.ini", directory) > 0);
    key_grammar();
    action_grammar();
    bounded_mutation();
    config_persistence(path);
    assert(!rmdir(directory));
    return 0;
}
