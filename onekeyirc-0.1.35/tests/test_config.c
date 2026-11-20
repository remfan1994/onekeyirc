#define _POSIX_C_SOURCE 200809L
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void write_file(const char *path, const void *data, size_t size, mode_t mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) perror(path);
    assert(fd >= 0);
    assert(write(fd, data, size) == (ssize_t)size);
    assert(fchmod(fd, mode) == 0);
    assert(close(fd) == 0);
}

static void terminal_signal_config(const char *path)
{
    static const char *const values[] = {"false", "true"};
    static const char *const invalid[] = {"", "2", "-1", "maybe", "true extra"};
    char text[256], error[256];
    Config c, loaded, before;
    for (int minimal = 0; minimal < 2; ++minimal) {
        if (minimal) config_minimal_defaults(&c);
        else config_defaults(&c);
        assert(!c.force_terminal_signals);
        const char legacy[] = "[input]\nsend_mode=review\n";
        write_file(path, legacy, sizeof legacy - 1u, 0600);
        assert(config_load(&c, path, error, sizeof error) == 1);
        assert(c.send_review && !c.force_terminal_signals);
        for (int force = 0; force < 2; ++force) {
            int n = snprintf(text, sizeof text, "[input]\nforce_terminal_signals=%s\n", values[force]);
            assert(n > 0 && (size_t)n < sizeof text);
            write_file(path, text, (size_t)n, 0600);
            assert(config_load(&c, path, error, sizeof error) == 1 && c.force_terminal_signals == force);
            assert(config_save(&c, path, error, sizeof error) == 1);
            if (minimal) config_minimal_defaults(&loaded);
            else config_defaults(&loaded);
            loaded.force_terminal_signals = !force;
            assert(config_load(&loaded, path, error, sizeof error) == 1);
            assert(!memcmp(&c, &loaded, sizeof c));
        }
        for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
            int n = snprintf(text, sizeof text, "[input]\nsend_mode=direct\nforce_terminal_signals=%s\n", invalid[i]);
            assert(n > 0 && (size_t)n < sizeof text);
            write_file(path, text, (size_t)n, 0600); before = c;
            assert(config_load(&c, path, error, sizeof error) == -1);
            assert(!memcmp(&before, &c, sizeof c) && strstr(error, "line 3"));
        }
        before = c;
        c.force_terminal_signals = -1;
        assert(config_validate(&c, error, sizeof error) == -1 && strstr(error, "force_terminal_signals"));
        c.force_terminal_signals = 2;
        assert(config_save(&c, path, error, sizeof error) == -1 && strstr(error, "force_terminal_signals"));
        c = before;
    }
    puts("terminal signal config: full/minimal default inheritance, legacy omission, true/false roundtrip, boolean validation and transactional rejection passed");
}

static void default_interface_config(const char *path)
{
    static const char *const names[] = {"ncurses", "idle"};
    static const char *const invalid[] = {"", "true", "1", "NCURSES", "minimal", "idle extra"};
    char text[256], error[256];
    Config c, loaded, before;
    for (int minimal = 0; minimal < 2; ++minimal) {
        if (minimal) config_minimal_defaults(&c);
        else config_defaults(&c);
        int default_interface = minimal ? CONFIG_INTERFACE_IDLE : CONFIG_INTERFACE_NCURSES;
        assert(c.default_interface == default_interface);
        const char *legacy = "[session]\nsingle_session=true\n";
        write_file(path, legacy, strlen(legacy), 0600);
        assert(config_load(&c, path, error, sizeof error) == 1);
        assert(c.single_session && c.default_interface == default_interface);
        for (int mode = CONFIG_INTERFACE_NCURSES; mode <= CONFIG_INTERFACE_IDLE; ++mode) {
            int n = snprintf(text, sizeof text, "[session]\ndefault_interface=%s\n", names[mode]);
            assert(n > 0 && (size_t)n < sizeof text);
            write_file(path, text, (size_t)n, 0600);
            assert(config_load(&c, path, error, sizeof error) == 1 && c.default_interface == mode);
            assert(config_save(&c, path, error, sizeof error) == 1);
            if (minimal) config_minimal_defaults(&loaded);
            else config_defaults(&loaded);
            loaded.default_interface = 1 - mode;
            assert(config_load(&loaded, path, error, sizeof error) == 1);
            assert(!memcmp(&c, &loaded, sizeof c));
        }
        for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
            int n = snprintf(text, sizeof text, "[session]\nsingle_session=false\ndefault_interface=%s\n", invalid[i]);
            assert(n > 0 && (size_t)n < sizeof text);
            write_file(path, text, (size_t)n, 0600);
            before = c;
            assert(config_load(&c, path, error, sizeof error) == -1);
            assert(!memcmp(&before, &c, sizeof c));
            assert(strstr(error, "line 3"));
        }
        before = c;
        c.default_interface = -1;
        assert(config_validate(&c, error, sizeof error) == -1 && strstr(error, "default_interface"));
        c.default_interface = 2;
        assert(config_save(&c, path, error, sizeof error) == -1 && strstr(error, "default_interface"));
        c = before;
    }
    puts("default interface: full/minimal defaults, legacy overlays, both saved values, exact enum grammar and transactional rejection passed");
}

static void mouse_config(const char *path)
{
    static const char *const values[] = { "true", "yes", "1", "false", "no", "0" };
    static const char *const invalid[] = { "", "2", "-1", "maybe", "true extra" };
    Config c, loaded, before;
    char text[128], error[256];
    const char *legacy = "[interface]\nuser_list=false\nhistory_lines=100\n";
    config_defaults(&c);
    assert(c.mouse == 1);
    write_file(path, legacy, strlen(legacy), 0600);
    assert(config_load(&c, path, error, sizeof(error)) == 1);
    assert(c.mouse == 1 && c.user_list == 0 && c.history_lines == 100);
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        int enabled = i < 3;
        int length = snprintf(text, sizeof(text), "[interface]\nmouse=%s\n", values[i]);
        assert(length > 0 && (size_t)length < sizeof(text));
        write_file(path, text, (size_t)length, 0600);
        config_defaults(&c);
        c.mouse = !enabled;
        assert(config_load(&c, path, error, sizeof(error)) == 1 && c.mouse == enabled);
        assert(config_save(&c, path, error, sizeof(error)) == 1);
        config_defaults(&loaded);
        loaded.mouse = !enabled;
        assert(config_load(&loaded, path, error, sizeof(error)) == 1);
        assert(!memcmp(&c, &loaded, sizeof(c)));
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        int length = snprintf(text, sizeof(text),
                              "[interface]\nuser_list=false\nmouse=%s\n", invalid[i]);
        assert(length > 0 && (size_t)length < sizeof(text));
        write_file(path, text, (size_t)length, 0600);
        config_defaults(&c);
        memcpy(&before, &c, sizeof(c));
        assert(config_load(&c, path, error, sizeof(error)) == -1);
        assert(!memcmp(&before, &c, sizeof(c)));
    }
    c.mouse = -1;
    assert(config_validate(&c, error, sizeof(error)) == -1);
    c.mouse = 2;
    assert(config_validate(&c, error, sizeof(error)) == -1);
    c.mouse = 0;
    assert(config_validate(&c, error, sizeof(error)) == 1);
    c.mouse = 1;
    assert(config_validate(&c, error, sizeof(error)) == 1);
}

static void output_and_hint_config(const char *path)
{
    Config c, loaded, before;
    char text[256], saved[32768], error[256];
    config_defaults(&c);
    assert(c.hints == 1 && c.shortcut_labels == 1 && c.output_full == 0);
    for (int full = 0; full <= 1; ++full) {
        for (int preferences = 0; preferences < 4; ++preferences) {
            int hints = preferences & 1, labels = preferences >> 1;
            config_defaults(&c); c.output_full = full; c.hints = hints; c.shortcut_labels = labels;
            assert(config_save(&c, path, error, sizeof error) == 1);
            FILE *file = fopen(path, "r"); assert(file);
            size_t size = fread(saved, 1, sizeof saved - 1, file);
            assert(!ferror(file) && feof(file) && !fclose(file)); saved[size] = 0;
            assert(strstr(saved, full ? "[output]\nformat=full\n" : "[output]\nformat=compact\n"));
            assert(strstr(saved, hints ? "hints=true\n" : "hints=false\n"));
            assert(strstr(saved, labels ? "shortcut_labels=true\n" : "shortcut_labels=false\n"));
            config_defaults(&loaded);
            assert(config_load(&loaded, path, error, sizeof error) == 1);
            assert(!memcmp(&loaded, &c, sizeof c));
        }
    }
    const char *bad_output[] = {"", "verbose", "FULL", "1", "full extra"};
    for (size_t i = 0; i < sizeof bad_output / sizeof bad_output[0]; ++i) {
        snprintf(text, sizeof text, "[connection]\nserver=changed.invalid\n[output]\nformat=%s\n", bad_output[i]);
        write_file(path, text, strlen(text), 0600); before = c;
        assert(config_load(&c, path, error, sizeof error) == -1);
        assert(!memcmp(&c, &before, sizeof c));
    }
    const char *bad_hints[] = {"", "2", "-1", "maybe", "false extra"};
    for (size_t i = 0; i < sizeof bad_hints / sizeof bad_hints[0]; ++i) {
        snprintf(text, sizeof text, "[connection]\nserver=changed.invalid\n[interface]\nhints=%s\n", bad_hints[i]);
        write_file(path, text, strlen(text), 0600); before = c;
        assert(config_load(&c, path, error, sizeof error) == -1);
        assert(!memcmp(&c, &before, sizeof c));
        snprintf(text, sizeof text, "[connection]\nserver=changed.invalid\n[interface]\nshortcut_labels=%s\n", bad_hints[i]);
        write_file(path, text, strlen(text), 0600); before = c;
        assert(config_load(&c, path, error, sizeof error) == -1);
        assert(!memcmp(&c, &before, sizeof c));
    }
    c.hints = -1; assert(config_validate(&c, error, sizeof error) == -1);
    c.hints = 2; assert(config_validate(&c, error, sizeof error) == -1);
    c.hints = 1; c.output_full = -1; assert(config_validate(&c, error, sizeof error) == -1);
    c.output_full = 2; assert(config_validate(&c, error, sizeof error) == -1);
    c.output_full = 0; c.shortcut_labels = -1; assert(config_validate(&c, error, sizeof error) == -1);
    c.shortcut_labels = 2; assert(config_validate(&c, error, sizeof error) == -1);
    c.shortcut_labels = 0; assert(config_validate(&c, error, sizeof error) == 1);
}

static void legacy_keymap_config(const char *path)
{
    static const char *const values[] = { "original", "vim" };
    static const char *const invalid[] = { "", "0", "1", "VIM", "Original", "vim extra", "other" };
    Config c, loaded, before;
    char text[128], error[256];
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        int length = snprintf(text, sizeof(text), "[interface]\nkeymap=%s\n", values[i]);
        assert(length > 0 && (size_t)length < sizeof(text));
        write_file(path, text, (size_t)length, 0600);
        config_defaults(&c);
        before = c;
        assert(config_load(&c, path, error, sizeof(error)) == 1);
        assert(!memcmp(&before, &c, sizeof(c)));
        assert(config_save(&c, path, error, sizeof(error)) == 1);
        char saved[32768];
        FILE *file = fopen(path, "r");
        assert(file);
        size_t size = fread(saved, 1, sizeof(saved) - 1, file);
        assert(!ferror(file) && feof(file));
        saved[size] = 0;
        assert(!fclose(file) && !strstr(saved, "keymap="));
        config_defaults(&loaded);
        assert(config_load(&loaded, path, error, sizeof(error)) == 1);
        assert(!memcmp(&c, &loaded, sizeof(c)));
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        int length = snprintf(text, sizeof(text),
                              "[interface]\nuser_list=false\nkeymap=%s\n", invalid[i]);
        assert(length > 0 && (size_t)length < sizeof(text));
        write_file(path, text, (size_t)length, 0600);
        config_defaults(&c);
        memcpy(&before, &c, sizeof(c));
        assert(config_load(&c, path, error, sizeof(error)) == -1);
        assert(!memcmp(&before, &c, sizeof(c)) && strstr(error, "line 3"));
    }
}

static void focus_style_config(const char *path)
{
    static const char *const values[] = { "reverse", "brackets" };
    static const char *const invalid[] = { "", "0", "1", "REVERSE", "Brackets", "bracket", "brackets extra" };
    const char *legacy = "[interface]\nmouse=false\nkeymap=vim\n";
    Config c, loaded, before;
    char text[128], error[256];
    config_defaults(&c);
    assert(c.focus_style == CONFIG_FOCUS_REVERSE);
    write_file(path, legacy, strlen(legacy), 0600);
    assert(config_load(&c, path, error, sizeof(error)) == 1);
    assert(c.focus_style == CONFIG_FOCUS_REVERSE && c.mouse == 0);
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        int expected = CONFIG_FOCUS_REVERSE;
        int length = snprintf(text, sizeof(text), "[interface]\nfocus_style=%s\n", values[i]);
        assert(length > 0 && (size_t)length < sizeof(text));
        write_file(path, text, (size_t)length, 0600);
        config_defaults(&c);
        assert(config_load(&c, path, error, sizeof(error)) == 1 && c.focus_style == expected);
        assert(config_save(&c, path, error, sizeof(error)) == 1);
        char saved[32768];
        FILE *file = fopen(path, "rb");
        assert(file);
        size_t size = fread(saved, 1, sizeof(saved) - 1, file);
        saved[size] = 0;
        assert(!ferror(file) && !fclose(file) && !strstr(saved, "focus_style="));
        config_defaults(&loaded);
        assert(config_load(&loaded, path, error, sizeof(error)) == 1);
        assert(!memcmp(&c, &loaded, sizeof(c)));
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        int length = snprintf(text, sizeof(text),
                              "[interface]\nuser_list=false\nfocus_style=%s\n", invalid[i]);
        assert(length > 0 && (size_t)length < sizeof(text));
        write_file(path, text, (size_t)length, 0600);
        config_defaults(&c);
        memcpy(&before, &c, sizeof(c));
        assert(config_load(&c, path, error, sizeof(error)) == -1);
        assert(!memcmp(&before, &c, sizeof(c)) && strstr(error, "line 3"));
    }
    c.focus_style = -1;
    assert(config_validate(&c, error, sizeof(error)) == -1);
    c.focus_style = 2;
    assert(config_validate(&c, error, sizeof(error)) == -1);
    c.focus_style = CONFIG_FOCUS_REVERSE;
    assert(config_validate(&c, error, sizeof(error)) == 1);
    c.focus_style = CONFIG_FOCUS_BRACKETS;
    assert(config_validate(&c, error, sizeof(error)) == 1);
}

static void ignore_config(const char *path)
{
    static const char *const invalid[] = {
        "", "*", "nick?", "nick@host", "nick!user", "!u@h", "n!@h", "n!u@",
        "n!u@h@x", "n!!u@h", "n!two users@h", "n!u@\177", "n!u@\303\251"
    };
    Config c, loaded, before;
    char text[4096], error[256], longest[CONFIG_IGNORE_SIZE + 1];
    size_t used;
    const char *legacy = "[interface]\nmouse=false\n";
    const char *several = "[ignore]\nmask=Alice\nmask=*!~user@*.Example.test\n"
                          "mask=*!*@cloak/#room;tag=$value\n";
    config_defaults(&c);
    assert(c.ignore_count == 0);
    c.ignore_count = 1;
    strcpy(c.ignores[0], "obsolete");
    write_file(path, legacy, strlen(legacy), 0600);
    assert(config_load(&c, path, error, sizeof(error)) == 1);
    assert(c.ignore_count == 0 && !c.ignores[0][0] && c.mouse == 0);
    write_file(path, several, strlen(several), 0600);
    config_defaults(&c);
    assert(config_load(&c, path, error, sizeof(error)) == 1);
    assert(c.ignore_count == 3 && !strcmp(c.ignores[0], "Alice"));
    assert(!strcmp(c.ignores[1], "*!~user@*.Example.test"));
    assert(!strcmp(c.ignores[2], "*!*@cloak/#room;tag=$value"));
    assert(config_save(&c, path, error, sizeof(error)) == 1);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof(error)) == 1);
    assert(!memcmp(&c, &loaded, sizeof(c)));

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        int length = snprintf(text, sizeof(text), "[interface]\nmouse=false\n[ignore]\nmask=%s\n", invalid[i]);
        assert(length > 0 && (size_t)length < sizeof(text));
        write_file(path, text, (size_t)length, 0600);
        before = c;
        assert(config_load(&c, path, error, sizeof(error)) == -1);
        assert(!memcmp(&before, &c, sizeof(c)) && strstr(error, "line 4"));
    }
    used = (size_t)snprintf(text, sizeof(text), "[ignore]\n");
    for (size_t i = 0; i < CONFIG_IGNORE_MAX; ++i) {
        int length = snprintf(text + used, sizeof(text) - used, "mask=User%02zu\n", i);
        assert(length > 0 && (size_t)length < sizeof(text) - used);
        used += (size_t)length;
    }
    write_file(path, text, used, 0600);
    config_defaults(&c);
    assert(config_load(&c, path, error, sizeof(error)) == 1 && c.ignore_count == CONFIG_IGNORE_MAX);
    assert(!strcmp(c.ignores[31], "User31"));
    assert(config_save(&c, path, error, sizeof(error)) == 1);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof(error)) == 1 && !memcmp(&c, &loaded, sizeof(c)));
    strcpy(text + used, "mask=TooMany\n");
    write_file(path, text, strlen(text), 0600);
    before = c;
    assert(config_load(&c, path, error, sizeof(error)) == -1);
    assert(!memcmp(&before, &c, sizeof(c)) && strstr(error, "line 34"));

    strcpy(longest, "*!*@");
    memset(longest + 4, 'h', CONFIG_IGNORE_SIZE - 5);
    longest[CONFIG_IGNORE_SIZE - 1] = 0;
    config_defaults(&c);
    c.ignore_count = 1;
    strcpy(c.ignores[0], longest);
    assert(config_validate(&c, error, sizeof(error)) == 1);
    assert(config_save(&c, path, error, sizeof(error)) == 1);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof(error)) == 1 && !memcmp(&c, &loaded, sizeof(c)));
    longest[CONFIG_IGNORE_SIZE - 1] = 'h'; longest[CONFIG_IGNORE_SIZE] = 0;
    int length = snprintf(text, sizeof(text), "[ignore]\nmask=%s\n", longest);
    assert(length > 0 && (size_t)length < sizeof(text));
    write_file(path, text, (size_t)length, 0600);
    before = c;
    assert(config_load(&c, path, error, sizeof(error)) == -1 && !memcmp(&before, &c, sizeof(c)));
    assert(strstr(error, "line 2"));
    assert(config_save(&c, path, error, sizeof(error)) == 1);
    memset(c.ignores[0], 'h', sizeof(c.ignores[0]));
    assert(config_validate(&c, error, sizeof(error)) == -1);
    assert(config_save(&c, path, error, sizeof(error)) == -1);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof(error)) == 1);
    assert(!memcmp(&before, &loaded, sizeof(loaded)) && "invalid ignore save replaced the previous config");
    c = before;
    c.ignore_count = CONFIG_IGNORE_MAX + 1;
    assert(config_validate(&c, error, sizeof(error)) == -1);
    c.ignore_count = (size_t)-1;
    assert(config_validate(&c, error, sizeof(error)) == -1);
    write_file(path, "[ignore]\n", strlen("[ignore]\n"), 0600);
    assert(config_load(&loaded, path, error, sizeof(error)) == 1 && loaded.ignore_count == 0);
    assert(config_save(&loaded, path, error, sizeof(error)) == 1);
    config_defaults(&c);
    assert(config_load(&c, path, error, sizeof(error)) == 1 && c.ignore_count == 0);
}

static void outer_spaces_config(const char *path)
{
    struct Field { const char *name; size_t offset, capacity; };
#define FIELD(name, member) {name, offsetof(Config, member), sizeof(((Config *)0)->member)}
    const struct Field fields[] = {
        FIELD("server", server), FIELD("nick", nick), FIELD("username", username),
        FIELD("realname", realname), FIELD("channel", channels), FIELD("auth", auth),
        FIELD("auth_user", auth_user), FIELD("password_file", password_file),
        FIELD("server_password_file", server_password_file), FIELD("ca_file", ca_file),
        {"mentions.word[1]", offsetof(Config, words), CONFIG_WORD_SIZE},
        {"ignore.mask[1]", offsetof(Config, ignores), CONFIG_IGNORE_SIZE}
    };
#undef FIELD
    Config original, candidate, loaded;
    char error[512];
    config_defaults(&original);
    strcpy(original.server, "test.invalid"); strcpy(original.nick, "Tester");
    strcpy(original.username, "tester"); strcpy(original.realname, "Two words");
    strcpy(original.channels, "#test"); strcpy(original.auth_user, "account name");
    strcpy(original.password_file, "/tmp/password with space");
    strcpy(original.server_password_file, "/tmp/server password");
    strcpy(original.ca_file, "/tmp/ca certificate");
    original.word_count = original.ignore_count = 1;
    strcpy(original.words[0], "two words"); strcpy(original.ignores[0], "Nick!*@host");
    assert(config_save(&original, path, error, sizeof(error)) == 1);
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        for (int side = 0; side < 3; ++side) {
            candidate = original;
            char *value = (char *)&candidate + fields[i].offset;
            size_t length = strlen(value);
            assert(length + 1 < fields[i].capacity);
            if (side == 0) { memmove(value + 1, value, length + 1); value[0] = ' '; }
            else if (side == 1) { value[length] = ' '; value[length + 1] = 0; }
            else memset(value, 'x', fields[i].capacity);
            assert(config_validate(&candidate, error, sizeof(error)) == -1);
            assert(strstr(error, fields[i].name) && strstr(error, side < 2 ? "spaces" : "limit"));
            assert(config_save(&candidate, path, error, sizeof(error)) == -1);
            config_defaults(&loaded);
            assert(config_load(&loaded, path, error, sizeof(error)) == 1);
            assert(!memcmp(&loaded, &original, sizeof(original)));
        }
    }
    /* Only ASCII space is trimmed by the file grammar. Valid UTF-8 spaces
     * survive, and interior ASCII spaces remain literal. */
    strcpy(original.realname, "\302\240two words\302\240");
    strcpy(original.words[0], "\302\240literal phrase\302\240");
    assert(config_save(&original, path, error, sizeof(error)) == 1);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof(error)) == 1);
    assert(!strcmp(loaded.realname, original.realname) && !strcmp(loaded.words[0], original.words[0]));
    const char *padded = "[connection]\nrealname=  Two words  \npassword_file=\t/tmp/interior space\t\n[mentions]\nword=  two words  \n";
    write_file(path, padded, strlen(padded), 0600);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof(error)) == 1);
    assert(!strcmp(loaded.realname, "Two words") && !strcmp(loaded.password_file, "/tmp/interior space") &&
           loaded.word_count == 1 && !strcmp(loaded.words[0], "two words"));
}

static void inline_credential_endings(const char *path)
{
    const struct Field { const char *key; size_t offset; } fields[] = {
        {"password", offsetof(Config, password)},
        {"server_password", offsetof(Config, server_password)}
    };
    const struct Case { const char *value; int valid; } cases[] = {
        {"  literal #;=\303\251  \n", 1},
        {"  literal #;=\303\251  \r\n", 1},
        {"  literal #;=\303\251  ", 1},
        {"value\r\r\n", 0},
        {"value\r", 0},
        {"before\rafter\n", 0},
        {"before\tafter\r\n", 0},
        {"before\177after", 0}
    };
    const char *const ordinary[] = {
        "[connection]\r\nrealname= \tTwo words \t\r\n",
        "[connection]\nrealname= \tTwo words \t\r\r\n",
        "[connection]\nrealname= \tTwo words \t\r"
    };
    Config baseline, loaded;
    char text[512], error[256];
    config_defaults(&baseline);
    strcpy(baseline.server, "original.invalid");
    strcpy(baseline.nick, "Original");
    strcpy(baseline.auth, "sasl");
    strcpy(baseline.auth_user, "fixture-account");
    strcpy(baseline.password, "prior-sasl");
    strcpy(baseline.server_password, "prior-server");
    baseline.word_count = baseline.ignore_count = 1;
    strcpy(baseline.words[0], "retained watch");
    strcpy(baseline.ignores[0], "KeepNick");
    assert(config_validate(&baseline, error, sizeof(error)) == 1);
    for (size_t field = 0; field < sizeof(fields) / sizeof(fields[0]); ++field) {
        for (size_t item = 0; item < sizeof(cases) / sizeof(cases[0]); ++item) {
            int length = snprintf(text, sizeof(text),
                                  "[interface]\nmouse=false\n[connection]\nserver=changed.invalid\n%s=%s",
                                  fields[field].key, cases[item].value);
            assert(length > 0 && (size_t)length < sizeof(text));
            write_file(path, text, (size_t)length, 0600);
            loaded = baseline;
            int result = config_load(&loaded, path, error, sizeof(error));
            if (cases[item].valid) {
                assert(result == 1);
                const char *value = (const char *)&loaded + fields[field].offset;
                assert(!strcmp(value, "  literal #;=\303\251  "));
                assert(!strcmp(loaded.server, "changed.invalid") && !loaded.mouse);
                assert(!strcmp(field ? loaded.password : loaded.server_password,
                               field ? "prior-sasl" : "prior-server"));
            } else {
                assert(result == -1 && strstr(error, "control"));
                assert(!memcmp(&loaded, &baseline, sizeof(loaded)));
            }
        }
    }
    /* Ordinary fields keep their historical trailing-whitespace trimming,
     * even when the final file line has no LF. Only inline secrets are literal. */
    for (size_t item = 0; item < sizeof(ordinary) / sizeof(ordinary[0]); ++item) {
        write_file(path, ordinary[item], strlen(ordinary[item]), 0600);
        loaded = baseline;
        assert(config_load(&loaded, path, error, sizeof(error)) == 1);
        assert(!strcmp(loaded.realname, "Two words"));
        assert(!strcmp(loaded.password, baseline.password));
        assert(!strcmp(loaded.server_password, baseline.server_password));
    }
}

static void profiles_config(const char *base)
{
    static const char *const invalid[] = {
        "profile=alpha.ini\n", "[connection]\nprofile=alpha.ini\n", "[servers]\nunknown=alpha.ini\n",
        "[servers]\nprofile=../escape.ini\n", "[servers]\nprofile=/absolute.ini\n",
        "[servers]\nprofile=.hidden\n", "[servers]\nprofile=a..b.ini\n", "[servers]\nprofile=two words.ini\n",
        "[servers]\nprofile=alpha.ini\nprofile=alpha.ini\n"
    };
    char primary[CONFIG_PATH_MAX], catalog[8192], lock_path[8192], directory[8192], victim[8192];
    char path[CONFIG_PATH_MAX], id[CONFIG_PROFILE_ID_SIZE] = "", ids[CONFIG_PROFILE_MAX][CONFIG_PROFILE_ID_SIZE];
    char error[512], text[4097], original[1024];
    ConfigProfiles profiles, before;
    Config config, loaded;
    int committed;
    struct stat st;
    assert(snprintf(primary, sizeof(primary), "%s.multi", base) > 0);
    assert(snprintf(catalog, sizeof(catalog), "%s.servers", primary) > 0);
    assert(snprintf(lock_path, sizeof(lock_path), "%s.servers.lock", primary) > 0);
    assert(snprintf(directory, sizeof(directory), "%s.d", primary) > 0);
    assert(snprintf(victim, sizeof(victim), "%s.victim", primary) > 0);
    memset(&profiles, 0x5a, sizeof(profiles));
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == 0 && !profiles.count);
    assert(access(catalog, F_OK) < 0 && access(lock_path, F_OK) < 0 && access(directory, F_OK) < 0);
    assert(config_profile_path(path, sizeof(path), primary, "alpha.ini", error, sizeof(error)) == 1);
    assert(config_profile_path(path, sizeof(path), primary, "../outside", error, sizeof(error)) == -1);
    assert(config_profile_path(path, 4, primary, "alpha.ini", error, sizeof(error)) == -1);
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        write_file(catalog, invalid[i], strlen(invalid[i]), 0600);
        before = profiles;
        assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == -1);
        assert(!memcmp(&profiles, &before, sizeof(profiles)) && strstr(error, "line"));
    }
    write_file(catalog, "[servers]\nprofile=a\0b\n", sizeof("[servers]\nprofile=a\0b\n") - 1, 0600);
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == -1);
    memset(text, 'x', sizeof(text));
    write_file(catalog, text, sizeof(text), 0600);
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == -1);
    write_file(catalog, "[servers]\n", strlen("[servers]\n"), 0666);
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == -1);
    assert(!unlink(catalog));
    write_file(victim, "not a catalog", strlen("not a catalog"), 0600);
    assert(!symlink(victim, catalog));
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == -1);
    assert(config_profile_remove(primary, "alpha.ini", &committed, error, sizeof(error)) == -1 && !committed);
    assert(!unlink(catalog) && !mkfifo(catalog, 0600));
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == -1);
    assert(!unlink(catalog) && !unlink(victim));

    config_defaults(&config);
    strcpy(config.server, "secondary.invalid");
    strcpy(config.nick, "Secondary");
    strcpy(config.auth, "sasl");
    strcpy(config.auth_user, "account");
    strcpy(config.password_file, "/must-not-read/secret");
    config.autoconnect = 1;
    for (size_t i = 0; i < CONFIG_PROFILE_MAX; ++i) {
        id[0] = 0;
        assert(config_profile_save(&config, primary, id, &committed, error, sizeof(error)) == 1 && committed);
        strcpy(ids[i], id);
        assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == 1 && profiles.count == i + 1);
        assert(!strcmp(profiles.ids[i], id));
        assert(config_profile_path(path, sizeof(path), primary, id, error, sizeof(error)) == 1);
        config_defaults(&loaded);
        assert(config_load(&loaded, path, error, sizeof(error)) == 1 && !strcmp(loaded.server, config.server));
        assert(!stat(path, &st) && (st.st_mode & 0777) == 0600);
    }
    assert(!stat(catalog, &st) && (st.st_mode & 0777) == 0600);
    assert(!stat(directory, &st) && (st.st_mode & 0777) == 0700);
    assert(access(primary, F_OK) < 0 && errno == ENOENT);
    assert(config_profiles_check(primary, error, sizeof(error)) == 1);
    id[0] = 0;
    assert(config_profile_save(&config, primary, id, &committed, error, sizeof(error)) == -1 && !committed && !*id);
    before = profiles;
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == 1 && !memcmp(&before, &profiles, sizeof(profiles)));
    strcpy(config.nick, "Changed");
    assert(config_profile_save(&config, primary, ids[0], &committed, error, sizeof(error)) == 1 && committed);
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == 1 && profiles.count == CONFIG_PROFILE_MAX);

    /* Another process holding the catalog lock makes Save fail promptly,
     * without replacing a profile or losing any startup references. */
    int lock_fd = open(lock_path, O_RDWR);
    struct flock lock;
    assert(lock_fd >= 0);
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    assert(!fcntl(lock_fd, F_SETLK, &lock));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        int child_committed;
        assert(config_profile_remove(primary, ids[0], &child_committed, error, sizeof(error)) == -1);
        assert(!child_committed && strstr(error, "busy"));
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(!close(lock_fd));
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == 1 && profiles.count == CONFIG_PROFILE_MAX);
    FILE *file = fopen(catalog, "r");
    assert(file);
    size_t n = fread(original, 1, sizeof(original) - 1, file);
    assert(!ferror(file) && feof(file) && !fclose(file));
    original[n] = 0;
    assert(config_profile_path(path, sizeof(path), primary, ids[1], error, sizeof(error)) == 1);
    assert(!unlink(path));
    assert(config_profiles_check(primary, error, sizeof(error)) == -1 && strstr(error, ids[1]));
    assert(config_save(&config, path, error, sizeof(error)) == 1);
    write_file(catalog, "[servers]\nprofile=missing.ini\n", strlen("[servers]\nprofile=missing.ini\n"), 0600);
    assert(config_profiles_check(primary, error, sizeof(error)) == -1 && strstr(error, "missing"));
    write_file(catalog, original, strlen(original), 0600);
    assert(config_profile_remove(primary, ids[0], &committed, error, sizeof(error)) == 1 && committed);
    assert(config_profile_path(path, sizeof(path), primary, ids[0], error, sizeof(error)) == 1 && !access(path, F_OK));
    assert(config_profiles_load(&profiles, primary, error, sizeof(error)) == 1 && profiles.count == CONFIG_PROFILE_MAX - 1);
    assert(config_profile_remove(primary, ids[0], &committed, error, sizeof(error)) == 1 && committed);
    for (size_t i = 0; i < CONFIG_PROFILE_MAX; ++i) {
        assert(config_profile_path(path, sizeof(path), primary, ids[i], error, sizeof(error)) == 1);
        assert(!unlink(path));
    }
    assert(!rmdir(directory) && !unlink(catalog) && !unlink(lock_path));
}

static void saved_default_config(const char *base)
{
    char primary[CONFIG_PATH_MAX], catalog[8192], lock_path[8192], directory[8192];
    char paths[3][CONFIG_PATH_MAX], ids[3][CONFIG_PROFILE_ID_SIZE] = {{0}};
    char text[1024], original[1024], error[512];
    Config config;
    ConfigProfiles profiles, before;
    int committed;
    struct stat st;
    assert(snprintf(primary, sizeof primary, "%s.defaults", base) > 0);
    assert(snprintf(catalog, sizeof catalog, "%s.servers", primary) > 0);
    assert(snprintf(lock_path, sizeof lock_path, "%s.servers.lock", primary) > 0);
    assert(snprintf(directory, sizeof directory, "%s.d", primary) > 0);
    assert(config_profile_set_default(primary, "", &committed, error, sizeof error) == 1 && committed);
    assert(access(catalog, F_OK) < 0 && access(lock_path, F_OK) < 0 && access(directory, F_OK) < 0);
    config_defaults(&config);
    strcpy(config.server, "saved.invalid");
    strcpy(config.nick, "SavedNick");
    for (size_t i = 0; i < 2; ++i) {
        assert(config_profile_save(&config, primary, ids[i], &committed, error, sizeof error) == 1 && committed);
        assert(config_profile_path(paths[i], sizeof paths[i], primary, ids[i], error, sizeof error) == 1);
    }
    assert(config_profile_set_default(primary, ids[1], &committed, error, sizeof error) == 1 && committed);
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && profiles.count == 2);
    assert(!strcmp(profiles.default_id, ids[1]));
    assert(!stat(catalog, &st) && (st.st_mode & 0777) == 0600);
    FILE *file = fopen(catalog, "r");
    assert(file);
    size_t n = fread(original, 1, sizeof original - 1, file);
    assert(!ferror(file) && feof(file) && !fclose(file)); original[n] = 0;
    const char *bad_ids[] = {"../escape", "/absolute", ".hidden", "two words", "missing.ini"};
    for (size_t i = 0; i < sizeof bad_ids / sizeof bad_ids[0]; ++i) {
        assert(config_profile_set_default(primary, bad_ids[i], &committed, error, sizeof error) == -1 && !committed);
        before = profiles;
        assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && !memcmp(&before, &profiles, sizeof profiles));
    }
    const char *bad_catalogs[] = {
        "[servers]\ndefault=\n", "[servers]\ndefault=../escape\n",
        "[servers]\ndefault=missing.ini\nprofile=other.ini\n",
        "[servers]\ndefault=alpha.ini\nprofile=alpha.ini\ndefault=alpha.ini\n",
        "[servers]\ndefault=alpha.ini\nprofile=alpha.ini\ndefault=beta.ini\nprofile=beta.ini\n"
    };
    for (size_t i = 0; i < sizeof bad_catalogs / sizeof bad_catalogs[0]; ++i) {
        write_file(catalog, bad_catalogs[i], strlen(bad_catalogs[i]), 0600);
        before = profiles;
        assert(config_profiles_load(&profiles, primary, error, sizeof error) == -1 && error[0]);
        assert(!memcmp(&before, &profiles, sizeof profiles));
    }
    /* The remembered profile uses its stable ID, including when the order changes. */
    int size = snprintf(text, sizeof text, "[servers]\ndefault=%s\nprofile=%s\nprofile=%s\n", ids[1], ids[1], ids[0]);
    assert(size > 0 && (size_t)size < sizeof text);
    write_file(catalog, text, (size_t)size, 0600);
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && !strcmp(profiles.default_id, ids[1]));
    assert(!strcmp(profiles.ids[0], ids[1]) && !strcmp(profiles.ids[1], ids[0]));
    write_file(catalog, original, n, 0600);

    strcpy(config.nick, "ChangedNick");
    assert(config_profile_save(&config, primary, ids[0], &committed, error, sizeof error) == 1 && committed);
    assert(config_profile_save(&config, primary, ids[2], &committed, error, sizeof error) == 1 && committed);
    assert(config_profile_path(paths[2], sizeof paths[2], primary, ids[2], error, sizeof error) == 1);
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && profiles.count == 3);
    assert(!strcmp(profiles.default_id, ids[1]));
    assert(config_profile_remove(primary, ids[0], &committed, error, sizeof error) == 1 && committed);
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && profiles.count == 2);
    assert(!strcmp(profiles.default_id, ids[1]));

    int lock_fd = open(lock_path, O_RDWR);
    struct flock lock = {0};
    assert(lock_fd >= 0); lock.l_type = F_WRLCK; lock.l_whence = SEEK_SET;
    assert(!fcntl(lock_fd, F_SETLK, &lock));
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        assert(config_profile_set_default(primary, "", &committed, error, sizeof error) == -1 && !committed);
        assert(strstr(error, "busy")); _exit(0);
    }
    int status; assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(!close(lock_fd));
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && !strcmp(profiles.default_id, ids[1]));
    assert(config_profile_set_default(primary, "", &committed, error, sizeof error) == 1 && committed);
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && !profiles.default_id[0] && profiles.count == 2);
    assert(config_profile_set_default(primary, ids[1], &committed, error, sizeof error) == 1 && committed);
    assert(config_profile_remove(primary, ids[1], &committed, error, sizeof error) == 1 && committed);
    assert(config_profiles_load(&profiles, primary, error, sizeof error) == 1 && !profiles.default_id[0] && profiles.count == 1);
    for (size_t i = 0; i < 3; ++i) assert(!unlink(paths[i]));
    assert(!rmdir(directory) && !unlink(catalog) && !unlink(lock_path));
    puts("config default: stable selection/order, atomic malformed/default validation, private persistence, unrelated edits preserved, removal clears selection and busy-lock failure passed");
}

static void automatic_command_config(const char *path)
{
    Config c, loaded, before;
    ConfigAutoCommand command, zero;
    char error[512], text[16384], longline[514];
    static const char *const good[] = {
        "/msg NickServ IDENTIFY account private#;=value  ", "/message helper hello",
        "/privmsg #room \303\251", "/join #room", "/j #room private-key",
        "/nick newnick", "/nickname NewNick", "/away busy now", "/away",
        "/back", "/unaway", "/whois friend", "/MSG helper literal $() `text`"
    };
    static const char *const bad[] = {
        "", "msg n secret", "/ msg n secret", "/msg", "/msg n", "/msg a,b private-secret",
        "/msg n private-secret\nJOIN #other", "/msg n private-secret\r", "/msg n \001ACTION private-secret\001",
        "/msg n \033[2J", "/msg n \300\200", "/join #a,#b", "/join #a key extra",
        "/join #a :private-secret", "/nick bad nick", "/whois", "/back extra",
        "/exec private-secret", "/raw private-secret", "/quote private-secret", "/save",
        "/connect private-secret", "/me private-secret", "/pm n", "//msg n private-secret"
    };
    memset(&zero, 0, sizeof zero);
    config_defaults(&c);
    assert(c.queue_notices == 1 && c.autocommands.enabled == 1 &&
           c.autocommands.delay_seconds == 15 && c.autocommands.interval_seconds == 2 && !c.autocommands.count);
    for (size_t i = 0; i < sizeof good / sizeof good[0]; ++i) {
        assert(config_autocommand_parse(good[i], &command, error, sizeof error) == 1 && !*error);
        assert(config_autocommand_parse(good[i], NULL, error, sizeof error) == 1);
    }
    assert(config_autocommand_parse(good[0], &command, error, sizeof error) == 1);
    assert(command.kind == CONFIG_AUTO_MSG && !strcmp(command.target, "NickServ") &&
           !strcmp(command.text, "IDENTIFY account private#;=value  "));
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        memset(&command, 0x7f, sizeof command);
        assert(config_autocommand_parse(bad[i], &command, error, sizeof error) == -1);
        assert(*error && !strstr(error, "private-secret") && !memcmp(&command, &zero, sizeof command));
    }
    memset(longline, 'x', sizeof longline); memcpy(longline, "/msg n ", 7);
    longline[511] = 0;
    assert(config_autocommand_parse(longline, NULL, error, sizeof error) == 1);
    longline[511] = 'x'; longline[512] = 0;
    assert(config_autocommand_parse(longline, NULL, error, sizeof error) == -1);
    memset(longline, 'x', sizeof longline);
    assert(config_autocommand_parse(longline, NULL, error, sizeof error) == -1);

    const char *valid = "[output]\nqueue_notices=false\n[autocommands]\nenabled=true\n"
                        "delay_seconds=20\ninterval_seconds=3\n"
                        "command=/msg NickServ IDENTIFY account private#;=value  \n"
                        "command=/msg ChanServ OP #room nickname\n";
    write_file(path, valid, strlen(valid), 0600);
    assert(config_load(&c, path, error, sizeof error) == 1);
    assert(!c.queue_notices && c.autocommands.count == 2 && c.autocommands.delay_seconds == 20 &&
           c.autocommands.interval_seconds == 3 && !strcmp(c.autocommands.lines[0], good[0]));
    assert(config_save(&c, path, error, sizeof error) == 1);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof error) == 1 && !memcmp(&c, &loaded, sizeof c));
    c.tls = 0;
    assert(config_validate(&c, error, sizeof error) == -1 && strstr(error, "TLS"));
    c.autocommands.enabled = 0;
    assert(config_validate(&c, error, sizeof error) == 1);
    c.tls = 1;
    before = c;
    const char *unreadable = "[autocommands]\nenabled=false\ncommand=/msg n private-secret\n";
    write_file(path, unreadable, strlen(unreadable), 0644);
    assert(config_load(&c, path, error, sizeof error) == -1 && strstr(error, "0600"));
    assert(!memcmp(&c, &before, sizeof c) && !strstr(error, "private-secret"));
    assert(!chmod(path, 0400));
    assert(config_load(&c, path, error, sizeof error) == 1 && c.autocommands.count == 1);
    assert(!chmod(path, 0600));

    /* Overlay lists replace, including when the new file omits the section. */
    const char *empty = "[connection]\nserver=different.invalid\n";
    write_file(path, empty, strlen(empty), 0600);
    assert(config_load(&c, path, error, sizeof error) == 1 && !c.autocommands.count);
    for (size_t i = 0; i < sizeof c.autocommands.lines; ++i)
        assert(((const unsigned char *)c.autocommands.lines)[i] == 0);
    const char *invalid[] = {
        "enabled=maybe", "delay_seconds=-1", "delay_seconds=301", "interval_seconds=1",
        "interval_seconds=61", "command=/raw private-secret", "command=/msg n private-secret\t",
        "command= /msg n private-secret", "unknown=private-secret"
    };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        assert(snprintf(text, sizeof text, "[autocommands]\ncommand=/msg n valid\n%s\n", invalid[i]) > 0);
        write_file(path, text, strlen(text), 0600); before = c;
        assert(config_load(&c, path, error, sizeof error) == -1 && !memcmp(&c, &before, sizeof c));
        assert(!strstr(error, "private-secret"));
    }
    strcpy(text, "[autocommands]\n");
    for (size_t i = 0; i < CONFIG_AUTOCOMMAND_MAX; ++i) strcat(text, "command=/whois friend\n");
    write_file(path, text, strlen(text), 0600);
    assert(config_load(&c, path, error, sizeof error) == 1 && c.autocommands.count == CONFIG_AUTOCOMMAND_MAX);
    strcat(text, "command=/whois one-too-many\n"); before = c;
    write_file(path, text, strlen(text), 0600);
    assert(config_load(&c, path, error, sizeof error) == -1 && !memcmp(&c, &before, sizeof c));
    c.queue_notices = 2; assert(config_validate(&c, error, sizeof error) == -1);
    c.queue_notices = 0; assert(config_validate(&c, error, sizeof error) == 1);
    c.autocommands.count = CONFIG_AUTOCOMMAND_MAX + 1u;
    assert(config_validate(&c, error, sizeof error) == -1);
    config_wipe(&c, sizeof c); config_wipe(&loaded, sizeof loaded); config_wipe(&command, sizeof command);
    puts("automatic configuration: exact bounded grammar, literal secret text, UTF-8/control rejection, private disabled lists, verified TLS, atomic overlay and round trip passed");
}

static void command_suggestion_config(const char *path)
{
    static const char *const modes[] = {"alphabetical", "common", "history", "frequent", "contextual"};
    Config c, loaded, before;
    char text[512], error[256], sidecar[CONFIG_PATH_MAX + 16];
    config_defaults(&c);
    assert(c.command_suggestions == CONFIG_SUGGEST_COMMON);
    for (int mode = 0; mode <= CONFIG_SUGGEST_CONTEXTUAL; ++mode) {
        snprintf(text, sizeof text, "[input]\ncommand_suggestions=%s\n", modes[mode]);
        write_file(path, text, strlen(text), 0600);
        assert(config_load(&c, path, error, sizeof error) == 1);
        assert(c.command_suggestions == mode && c.command_usage_count == 0);
        assert(config_save(&c, path, error, sizeof error) == 1);
        config_defaults(&loaded);
        assert(config_load(&loaded, path, error, sizeof error) == 1 && !memcmp(&loaded, &c, sizeof c));
    }
    c.command_usage_count = 2;
    strcpy(c.command_usage[0].name, "join"); c.command_usage[0].count = 3;
    strcpy(c.command_usage[1].name, "summary"); c.command_usage[1].count = 1;
    assert(config_save(&c, path, error, sizeof error) == 1);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof error) == 1);
    assert(loaded.command_suggestions == c.command_suggestions && loaded.command_usage_count == 0);
    FILE *saved = fopen(path, "r"); assert(saved);
    char saved_text[32768]; size_t bytes = fread(saved_text, 1, sizeof saved_text - 1u, saved);
    assert(!ferror(saved) && feof(saved) && !fclose(saved)); saved_text[bytes] = 0;
    assert(!strstr(saved_text, "[command_usage]") && !strstr(saved_text, "join=3"));
    static const char *const invalid[] = {
        "[input]\ncommand_suggestions=unknown\n", "[command_usage]\njoin=3\n"
    };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        write_file(path, invalid[i], strlen(invalid[i]), 0600);
        before = c;
        assert(config_load(&c, path, error, sizeof error) == -1 && !memcmp(&c, &before, sizeof c));
    }
    snprintf(sidecar, sizeof sidecar, "%s.commands", path);
    assert(config_command_usage_load(&c, path, error, sizeof error) == 0);
    assert(config_command_usage_save(&c, path, error, sizeof error) == 1);
    struct stat statbuf;
    assert(!stat(sidecar, &statbuf) && (statbuf.st_mode & 0777) == 0600);
    config_defaults(&loaded);
    strcpy(loaded.nick, "Unchanged");
    assert(config_command_usage_load(&loaded, path, error, sizeof error) == 1);
    assert(!strcmp(loaded.nick, "Unchanged") && loaded.command_usage_count == 2 && loaded.command_usage[0].count == 3);
    loaded.command_usage[0].count = 0;
    assert(config_command_usage_save(&loaded, path, error, sizeof error) == 1);
    assert(config_command_usage_load(&c, path, error, sizeof error) == 1 && c.command_usage[0].count == 0);
    assert(!chmod(sidecar, 0644));
    before = c;
    assert(config_command_usage_load(&c, path, error, sizeof error) == -1 && !memcmp(&c, &before, sizeof c));
    static const char *const invalid_counts[] = {
        "join=2\njoin=3\n", "join=-1\n", "join=2147483648\n", "join #secret=3\n", "JOIN=3\n"
    };
    for (size_t i = 0; i < sizeof invalid_counts / sizeof invalid_counts[0]; ++i) {
        write_file(sidecar, invalid_counts[i], strlen(invalid_counts[i]), 0600);
        assert(config_command_usage_load(&c, path, error, sizeof error) == -1 && !memcmp(&c, &before, sizeof c));
    }
    assert(!unlink(sidecar));
    assert(!symlink(path, sidecar));
    assert(config_command_usage_load(&c, path, error, sizeof error) == -1);
    assert(config_command_usage_save(&c, path, error, sizeof error) == -1);
    assert(!unlink(sidecar));
    assert(config_command_usage_save(&c, "", error, sizeof error) == 0);
    assert(config_command_usage_load(&c, "", error, sizeof error) == 0);
}

static void log_path_cases(void)
{
    char root[] = "/tmp/vipirc-log-paths-XXXXXX";
    char parent[CONFIG_PATH_MAX], config[CONFIG_PATH_MAX], log[CONFIG_PATH_MAX];
    char alias[CONFIG_PATH_MAX], indirect[CONFIG_PATH_MAX], nested[CONFIG_PATH_MAX];
    char nested_log[CONFIG_PATH_MAX], canary[CONFIG_PATH_MAX], missing[CONFIG_PATH_MAX];
    char error[256], bytes[32];
    struct stat st;
    assert(mkdtemp(root));
    assert(snprintf(parent, sizeof parent, "%s/private", root) > 0);
    assert(!mkdir(parent, 0700));
    assert(snprintf(config, sizeof config, "%s/config.ini", parent) > 0);
    assert(snprintf(log, sizeof log, "%s.log", config) > 0);
    mode_t saved_mask = umask(0);
    int fd = config_open_log(config, error, sizeof error);
    (void)umask(saved_mask);
    assert(fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode));
    assert(st.st_uid == getuid() && st.st_nlink == 1 && (st.st_mode & 07777) == 0600);
    int flags = fcntl(fd, F_GETFL);
    assert(flags >= 0 && (flags & O_ACCMODE) == O_WRONLY && (flags & O_APPEND) && (flags & O_NONBLOCK));
    assert(fcntl(fd, F_GETFD) & FD_CLOEXEC);
    assert(write(fd, "first\n", 6) == 6 && !close(fd));
    assert(access(config, F_OK) < 0 && errno == ENOENT);
    fd = config_open_log(config, error, sizeof error);
    assert(fd >= 0 && write(fd, "second\n", 7) == 7 && !close(fd));
    fd = open(log, O_RDONLY);
    memset(bytes, 0, sizeof bytes);
    assert(fd >= 0 && read(fd, bytes, sizeof bytes) == 13 && !strcmp(bytes, "first\nsecond\n") && !close(fd));
    fd = config_open_log_read(config, error, sizeof error);
    memset(bytes, 0, sizeof bytes);
    assert(fd >= 0 && read(fd, bytes, sizeof bytes) == 13 && !strcmp(bytes, "first\nsecond\n"));
    flags = fcntl(fd, F_GETFL);
    assert(flags >= 0 && (flags & O_ACCMODE) == O_RDONLY && (flags & O_NONBLOCK));
    assert(fcntl(fd, F_GETFD) & FD_CLOEXEC);
    assert(!close(fd));
    assert(!unlink(log));

    /* A log suffix is derived from the config basename. Neither the suffix
     * nor any intermediate directory may be redirected through a symlink. */
    assert(snprintf(canary, sizeof canary, "%s/canary", root) > 0);
    write_file(canary, "unchanged", 9, 0600);
    assert(!symlink(canary, log));
    assert(config_open_log(config, error, sizeof error) < 0);
    assert(config_open_log_read(config, error, sizeof error) < 0);
    fd = open(canary, O_RDONLY);
    memset(bytes, 0, sizeof bytes);
    assert(fd >= 0 && read(fd, bytes, sizeof bytes) == 9 && !strcmp(bytes, "unchanged") && !close(fd));
    assert(!unlink(log) && !unlink(canary));
    assert(snprintf(nested, sizeof nested, "%s/nested", parent) > 0 && !mkdir(nested, 0700));
    assert(snprintf(alias, sizeof alias, "%s/alias", root) > 0 && !symlink(parent, alias));
    assert(snprintf(indirect, sizeof indirect, "%s/config.ini", alias) > 0);
    assert(config_open_log(indirect, error, sizeof error) < 0);
    assert(config_open_log_read(indirect, error, sizeof error) < 0);
    assert(snprintf(indirect, sizeof indirect, "%s/nested/config.ini", alias) > 0);
    assert(config_open_log(indirect, error, sizeof error) < 0);
    assert(snprintf(nested_log, sizeof nested_log, "%s/config.ini.log", nested) > 0);
    assert(access(nested_log, F_OK) < 0 && errno == ENOENT);
    assert(!unlink(alias) && !rmdir(nested));

    const mode_t unsafe_modes[] = {0755, 0710, 0500, 01700};
    for (size_t i = 0; i < sizeof unsafe_modes / sizeof unsafe_modes[0]; ++i) {
        assert(!chmod(parent, unsafe_modes[i]));
        assert(config_open_log(config, error, sizeof error) < 0);
        assert(config_open_log_read(config, error, sizeof error) < 0);
        assert(!chmod(parent, 0700));
        assert(access(log, F_OK) < 0 && errno == ENOENT);
    }
    assert(snprintf(missing, sizeof missing, "%s/missing", root) > 0);
    assert(snprintf(indirect, sizeof indirect, "%s/config.ini", missing) > 0);
    assert(config_open_log(indirect, error, sizeof error) < 0);
    assert(access(missing, F_OK) < 0 && errno == ENOENT);
    assert(snprintf(indirect, sizeof indirect, "%s/../config.ini", parent) > 0);
    assert(config_open_log(indirect, error, sizeof error) < 0);
    size_t prefix = strlen(parent);
    assert(prefix + 1u + 252u < sizeof indirect);
    memcpy(indirect, parent, prefix); indirect[prefix++] = '/';
    memset(indirect + prefix, 'a', 252); indirect[prefix + 252u] = 0;
    assert(config_open_log(indirect, error, sizeof error) < 0 && strstr(error, "filename"));
    assert(config_open_log("", error, sizeof error) < 0);
    assert(config_open_log(NULL, error, sizeof error) < 0);

    /* A FIFO with no reader must fail promptly, before the App's fstat gate.
     * The alarm makes a blocking-open regression terminate instead of hang. */
    assert(!mkfifo(log, 0600));
    (void)alarm(3);
    fd = config_open_log(config, error, sizeof error);
    (void)alarm(0);
    assert(fd < 0);
    int reader = open(log, O_RDONLY | O_NONBLOCK);
    assert(reader >= 0);
    fd = config_open_log(config, error, sizeof error);
    assert(fd >= 0 && !fstat(fd, &st) && S_ISFIFO(st.st_mode));
    assert(fcntl(fd, F_GETFL) & O_NONBLOCK);
    assert(config_open_log_read(config, error, sizeof error) < 0);
    assert(!close(fd) && !close(reader) && !unlink(log));

    /* The catch-up reader accepts only the same bounded regular log that the
     * writer can safely maintain. A sparse oversized file must not turn the
     * history reader into an unbounded disk scanner. */
    fd = open(log, O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0 && !ftruncate(fd, (off_t)(16u * 1024u * 1024u + 1u)) && !close(fd));
    assert(config_open_log_read(config, error, sizeof error) < 0);
    assert(!unlink(log));

    assert(!rmdir(parent) && !rmdir(root));
    puts("log paths: private append/read, symlink ancestors/leaf refusal, exact parent mode, no directory creation, bounded suffix, FIFO refusal and 16 MiB reader ceiling passed");
}

int main(void)
{
    char dir[] = "/tmp/vipirc-config-XXXXXX";
    char path[4096], other[4096], error[256], secret[CONFIG_SECRET_MAX + 1];
    Config c, loaded, original;
    struct stat st;
    log_path_cases();
    assert(mkdtemp(dir));
    assert(snprintf(path, sizeof path, "%s/config.ini", dir) > 0);
    assert(snprintf(other, sizeof other, "%s/password", dir) > 0);
    config_defaults(&c);
    assert(c.tls && c.port == 6697 && c.user_list && c.mouse && c.hints && c.shortcut_labels && !c.output_full &&
           c.focus_style == CONFIG_FOCUS_REVERSE && c.history_lines == 500);
    assert(config_load(&c, path, error, sizeof error) == 0);
    strcpy(c.server, "irc.example.test");
    strcpy(c.nick, "tutankh-aten");
    strcpy(c.channels, "#one,#two");
    strcpy(c.words[0], "Strawberry Creek");
    strcpy(c.words[1], "literal # comment");
    c.word_count = 2;
    c.autoconnect = 1;
    assert(config_save(&c, path, error, sizeof error) == 1);
    assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    config_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof error) == 1);
    assert(memcmp(&loaded, &c, sizeof c) == 0);
    terminal_signal_config(path);
    default_interface_config(path);
    mouse_config(path);
    output_and_hint_config(path);
    command_suggestion_config(path);
    automatic_command_config(path);
    legacy_keymap_config(path);
    focus_style_config(path);
    ignore_config(path);
    outer_spaces_config(path);
    inline_credential_endings(path);
    profiles_config(path);
    saved_default_config(path);

    /* Invalid input never partially overwrites the caller's configuration. */
    original = loaded;
    write_file(path, "[connection]\nserver=changed\nunknown=true\n", strlen("[connection]\nserver=changed\nunknown=true\n"), 0600);
    assert(config_load(&loaded, path, error, sizeof error) == -1);
    assert(memcmp(&loaded, &original, sizeof loaded) == 0);
    assert(strstr(error, "line 3"));
    write_file(path, "[connection]\nport=0\n", strlen("[connection]\nport=0\n"), 0600);
    assert(config_load(&loaded, path, error, sizeof error) == -1);
    write_file(path, "[connection]\nnick=x\0y\n", sizeof("[connection]\nnick=x\0y\n") - 1, 0600);
    assert(config_load(&loaded, path, error, sizeof error) == -1);
    write_file(path, "[connection]\nserver=x\n", strlen("[connection]\nserver=x\n"), 0666);
    assert(config_load(&loaded, path, error, sizeof error) == -1);
    assert(config_save(&c, path, error, sizeof error) == -1);
    assert(unlink(path) == 0);

    /* Neither reading nor replacing symlinked files is allowed. */
    write_file(other, "a secret\n", 9, 0600);
    assert(symlink(other, path) == 0);
    assert(config_load(&loaded, path, error, sizeof error) == -1);
    assert(config_save(&c, path, error, sizeof error) == -1);
    assert(config_read_secret(path, secret, sizeof secret, error, sizeof error) == -1);
    assert(unlink(path) == 0);
    assert(config_read_secret(other, secret, sizeof secret, error, sizeof error) == 1);
    assert(strcmp(secret, "a secret") == 0);
    config_wipe(secret, sizeof secret);
    for (size_t i = 0; i < sizeof secret; ++i) assert(secret[i] == 0);
    assert(chmod(other, 0644) == 0);
    assert(config_read_secret(other, secret, sizeof secret, error, sizeof error) == -1);
    assert(chmod(other, 0700) == 0);
    assert(config_read_secret(other, secret, sizeof secret, error, sizeof error) == -1);
    write_file(other, "two\nlines\n", 10, 0600);
    assert(config_read_secret(other, secret, sizeof secret, error, sizeof error) == -1);
    write_file(other, "crlf\r\n", 6, 0400);
    assert(config_read_secret(other, secret, sizeof secret, error, sizeof error) == 1);
    assert(strcmp(secret, "crlf") == 0);
    assert(chmod(other, 0600) == 0);
    write_file(other, "\0x", 2, 0600);
    assert(config_read_secret(other, secret, sizeof secret, error, sizeof error) == -1);
    assert(unlink(other) == 0);
    assert(mkfifo(other, 0600) == 0);
    assert(config_read_secret(other, secret, sizeof secret, error, sizeof error) == -1);
    assert(unlink(other) == 0);

    /* A malicious config cannot downgrade automatic authentication to plaintext. */
    c.tls = 0;
    strcpy(c.auth, "sasl");
    strcpy(c.auth_user, "test-account");
    strcpy(c.password_file, other);
    assert(config_validate(&c, error, sizeof error) == -1);
    c.tls = 1;
    assert(config_validate(&c, error, sizeof error) == 1);
    strcpy(c.server, "::1");
    assert(config_validate(&c, error, sizeof error) == 1);
    strcpy(c.server, "irc://bad");
    assert(config_validate(&c, error, sizeof error) == -1);
    strcpy(c.server, "irc.example.test");
    strcpy(c.nick, "bad nick");
    assert(config_validate(&c, error, sizeof error) == -1);
    strcpy(c.nick, "tester");
    strcpy(c.username, "bad:user");
    assert(config_validate(&c, error, sizeof error) == -1);
    strcpy(c.username, "tester");
    strcpy(c.channels, "#one,,#two");
    assert(config_validate(&c, error, sizeof error) == -1);
    strcpy(c.channels, "#one,");
    assert(config_validate(&c, error, sizeof error) == -1);
    strcpy(c.channels, "#one,#two");
    assert(config_validate(&c, error, sizeof error) == 1);
    assert(config_save(&c, path, error, sizeof error) == 1);
    assert(unlink(path) == 0);
    assert(chmod(dir, 0755) == 0);
    assert(config_save(&c, path, error, sizeof error) == -1);
    assert(chmod(dir, 0700) == 0);
    assert(rmdir(dir) == 0);
    Config valid = c;
    c.port = 0;
    assert(config_validate(&c, error, sizeof error) == -1 && strstr(error, "port") && strstr(error, "65535"));
    c = valid; c.tls = 9;
    assert(config_validate(&c, error, sizeof error) == -1 && strstr(error, "tls") && strstr(error, "plaintext"));
    c = valid; c.history_lines = 5001;
    assert(config_validate(&c, error, sizeof error) == -1 && strstr(error, "history") && strstr(error, "5000"));
    c = valid; strcpy(c.password, "do-not-disclose-this-password");
    assert(config_validate(&c, error, sizeof error) == -1 && strstr(error, "SASL password"));
    assert(!strstr(error, c.password));
    puts("config: private files, round trip, bounded ignores/catalogs, profile transactions/locks, strict parser, field-specific redacted diagnostics, injection and downgrade checks passed");
    return 0;
}
