#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f && fchmod(fileno(f), 0600) == 0);
    assert(fputs(text, f) >= 0 && fclose(f) == 0);
}

static void read_text(const char *path, char *text, size_t cap)
{
    FILE *f = fopen(path, "r");
    assert(f);
    size_t n = fread(text, 1, cap - 1, f);
    assert(!ferror(f) && feof(f) && fclose(f) == 0);
    text[n] = 0;
}

int main(void)
{
    char dir[] = "/tmp/onekey-config-XXXXXXXX", path[512], error[512], text[32768];
    char native_path[CONFIG_PATH_MAX], full_path[CONFIG_PATH_MAX];
    Config c, loaded, before;
    assert(mkdtemp(dir));
    assert(snprintf(path, sizeof path, "%s/config.ini", dir) > 0);
    assert(setenv("XDG_CONFIG_HOME", dir, 1) == 0);
    assert(config_minimal_path(native_path, sizeof native_path, error, sizeof error) == 1);
    assert(config_default_path(full_path, sizeof full_path, error, sizeof error) == 1);
    assert(strstr(native_path, "/onekeyirc/config.ini") && strstr(full_path, "/vipirc/config.ini"));
    assert(strcmp(native_path, full_path));

    config_minimal_defaults(&c);
    assert(c.default_interface == CONFIG_INTERFACE_IDLE);
    assert(!c.use_signals);
    assert(c.minimal && !c.send_review && !c.composer_fullscreen && !c.output_full && c.hints && c.shortcut_labels && !c.mouse && !c.user_list && c.history_lines == 100);
    strcpy(c.server, "example.invalid");
    strcpy(c.nick, "tester");
    strcpy(c.channels, "#one,#two");
    c.autoconnect = c.send_review = c.composer_fullscreen = c.output_full = c.use_signals = 1;
    strcpy(c.auth, "sasl");
    strcpy(c.auth_user, "account");
    strcpy(c.password, " literal # = password ");
    assert(config_save(&c, path, error, sizeof error) == 1);
    read_text(path, text, sizeof text);
    assert(strstr(text, "# onekeyirc configuration.") && strstr(text, "[input]\nsend_mode=review") && strstr(text, "composer_mode=fullscreen"));
    assert(strstr(text, "use_signals=true\n") && !strstr(text, "force_terminal_signals="));
    assert(strstr(text, "[history]\nlines=100") && strstr(text, "[output]\nformat=full"));
    assert(strstr(text, "[session]\nsingle_session=false\ndefault_interface=idle\n"));
    assert(!strstr(text, "[interface]") && !strstr(text, "mouse=") && !strstr(text, "user_list=") && !strstr(text, "hints=") && !strstr(text, "shortcut_labels="));
    config_minimal_defaults(&loaded);
    assert(config_load(&loaded, path, error, sizeof error) == 1);
    assert(!memcmp(&c, &loaded, sizeof c));
    struct stat st;
    assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    config_defaults(&loaded);
    assert(loaded.default_interface == CONFIG_INTERFACE_NCURSES);
    assert(config_load(&loaded, path, error, sizeof error) == 1);
    assert(!loaded.minimal && loaded.send_review && loaded.output_full && loaded.use_signals && !strcmp(loaded.password, c.password));
    assert(loaded.default_interface == CONFIG_INTERFACE_IDLE);

    const char *bad[] = {"", "true", "two", "DIRECT", "review extra"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        snprintf(text, sizeof text, "[connection]\nnick=changed\n[input]\nsend_mode=%s\n", bad[i]);
        write_text(path, text);
        before = c;
        assert(config_load(&c, path, error, sizeof error) == -1);
        assert(!memcmp(&c, &before, sizeof c));
    }
    const char *bad_composer[] = {"", "full", "LINE", "1", "fullscreen extra"};
    for (size_t i = 0; i < sizeof bad_composer / sizeof bad_composer[0]; ++i) {
        snprintf(text, sizeof text, "[input]\ncomposer_mode=%s\n", bad_composer[i]);
        write_text(path, text); before = c;
        assert(config_load(&c, path, error, sizeof error) == -1);
        assert(!memcmp(&c, &before, sizeof c));
    }
    write_text(path, "[input]\ncomposer_mode=line\n");
    assert(config_load(&c, path, error, sizeof error) == 1 && !c.composer_fullscreen);
    const char *bad_output[] = {"", "verbose", "FULL", "1", "full extra"};
    for (size_t i = 0; i < sizeof bad_output / sizeof bad_output[0]; ++i) {
        snprintf(text, sizeof text, "[connection]\nnick=changed\n[output]\nformat=%s\n", bad_output[i]);
        write_text(path, text); before = c;
        assert(config_load(&c, path, error, sizeof error) == -1);
        assert(!memcmp(&c, &before, sizeof c));
    }
    write_text(path, "[output]\nformat=compact\n");
    assert(config_load(&c, path, error, sizeof error) == 1 && !c.output_full);
    write_text(path, "[interface]\nmouse=false\n[input]\nsend_mode=review\n");
    before = c;
    assert(config_load(&c, path, error, sizeof error) == -1);
    assert(!memcmp(&c, &before, sizeof c) && strstr(error, "[interface]"));
    config_defaults(&c);
    assert(config_load(&c, path, error, sizeof error) == 1 && !c.mouse && c.send_review);
    assert(config_save(&c, path, error, sizeof error) == 1);
    read_text(path, text, sizeof text);
    assert(strstr(text, "[interface]") && strstr(text, "mouse=false") && strstr(text, "send_mode=review"));

    write_text(path, "[input]\nsend_mode=direct\n");
    assert(config_load(&c, path, error, sizeof error) == 1 && !c.send_review);
    c.send_review = 2;
    assert(config_validate(&c, error, sizeof error) == -1);
    c.send_review = 0;
    c.composer_fullscreen = 2;
    assert(config_validate(&c, error, sizeof error) == -1);
    c.composer_fullscreen = 0;
    c.use_signals = 2;
    assert(config_validate(&c, error, sizeof error) == -1);
    c.use_signals = 0;
    c.minimal = -1;
    assert(config_validate(&c, error, sizeof error) == -1);
    config_wipe(&c, sizeof c);
    config_wipe(&loaded, sizeof loaded);
    config_wipe(&before, sizeof before);
    assert(unlink(path) == 0 && rmdir(dir) == 0);
    puts("idle config: independent paths, direct/review, line/fullscreen and compact/full defaults, native/full serialization, exact private secrets, compatible shared fields, atomic invalid-input rejection and preserved full-interface options passed");
    return 0;
}
