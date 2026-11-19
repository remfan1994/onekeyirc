#define _POSIX_C_SOURCE 200809L
#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static Config configured(int type)
{
    Config c;
    config_defaults(&c);
    strcpy(c.server, "bouncer.example.invalid");
    strcpy(c.nick, "Alice");
    c.bouncer_type = type;
    strcpy(c.bouncer_user, "Alice_1");
    strcpy(c.bouncer_network, "ircnow");
    if (type == CONFIG_BOUNCER_ZNC) strcpy(c.server_password, "BNC_TEST_PASSWORD:with spaces");
    if (type == CONFIG_BOUNCER_SOJU) {
        strcpy(c.auth, "sasl");
        strcpy(c.password, "BNC_TEST_PASSWORD:with spaces");
    }
    return c;
}

static void identity(void)
{
    char out[192], error[512];
    Config c = configured(CONFIG_BOUNCER_ZNC);
    assert(config_validate(&c, error, sizeof error) == 1);
    assert(!c.bouncer_away);
    assert(config_bouncer_identity(&c, out, sizeof out) == 1);
    assert(!strcmp(out, "Alice_1/ircnow"));
    strcpy(c.bouncer_client, "laptop-1");
    assert(config_bouncer_identity(&c, out, sizeof out) == 1);
    assert(!strcmp(out, "Alice_1@laptop-1/ircnow"));
    c.bouncer_type = CONFIG_BOUNCER_SOJU;
    assert(config_bouncer_identity(&c, out, sizeof out) == 1);
    assert(!strcmp(out, "Alice_1/ircnow@laptop-1"));
    c.bouncer_client[0] = 0;
    assert(config_bouncer_identity(&c, out, sizeof out) == 1);
    assert(!strcmp(out, "Alice_1/ircnow"));
    for (size_t cap = 0; cap <= strlen("Alice_1/ircnow"); ++cap) {
        memset(out, 'Q', sizeof out);
        assert(config_bouncer_identity(&c, out, cap) == -1);
        if (cap) assert(!out[0]);
        assert(out[sizeof out - 1] == 'Q');
    }
    assert(config_bouncer_identity(&c, NULL, 0) == -1);
    assert(config_bouncer_identity(NULL, out, sizeof out) == -1 && !out[0]);
    c.bouncer_type = CONFIG_BOUNCER_NONE;
    assert(config_bouncer_identity(&c, out, sizeof out) == 0 && !out[0]);
    c.bouncer_type = 99;
    assert(config_bouncer_identity(&c, out, sizeof out) == -1 && !out[0]);
    c = configured(CONFIG_BOUNCER_SOJU);
    memset(c.bouncer_user, 'a', 63); c.bouncer_user[63] = 0;
    memset(c.bouncer_network, 'n', 63); c.bouncer_network[63] = 0;
    memset(c.bouncer_client, 'c', 63); c.bouncer_client[63] = 0;
    assert(config_validate(&c, error, sizeof error) == 1);
    assert(config_bouncer_identity(&c, out, sizeof out) == 1 && strlen(out) == 191);
    c.bouncer_user[63] = 'a';
    assert(config_bouncer_identity(&c, out, sizeof out) == -1 && !out[0]);
    assert(config_validate(&c, error, sizeof error) == -1);
    config_wipe(&c, sizeof c);
}

static void rejected_components(void)
{
    static const char *const invalid[] = {
        "bad/name", "bad@name", "bad:name", "bad name", "bad\tname", "bad\nname",
        "bad\rname", "bad\001name", "bad,name", "bad*name", "bad?name", "bad=name",
        "bad\\name", "bad\177name", "bad\303\251name"
    };
    for (int type = CONFIG_BOUNCER_ZNC; type <= CONFIG_BOUNCER_SOJU; ++type) {
        for (size_t which = 0; which < 3; ++which) {
            for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
                Config c = configured(type);
                char *part = which == 0 ? c.bouncer_user : which == 1 ? c.bouncer_network : c.bouncer_client;
                strcpy(part, invalid[i]);
                char out[192], error[512];
                assert(config_bouncer_identity(&c, out, sizeof out) == -1 && !out[0]);
                assert(config_validate(&c, error, sizeof error) == -1);
                assert(!strstr(error, "BNC_TEST_PASSWORD"));
                config_wipe(&c, sizeof c);
            }
        }
        Config c = configured(type); char error[512];
        c.bouncer_user[0] = 0;
        assert(config_validate(&c, error, sizeof error) == -1);
        c = configured(type); c.bouncer_network[0] = 0;
        assert(config_validate(&c, error, sizeof error) == -1);
        c = configured(type); c.tls = 0;
        assert(config_validate(&c, error, sizeof error) == -1);
        c = configured(type); c.bouncer_away = 2;
        assert(config_validate(&c, error, sizeof error) == -1);
        c = configured(type); c.bouncer_away = 1;
        assert(config_validate(&c, error, sizeof error) == 1);
        strcpy(c.auth_user, "wrong-upstream-account");
        assert(config_validate(&c, error, sizeof error) == -1);
        config_wipe(&c, sizeof c);
    }
}

static void credentials(void)
{
    char error[512], login[192];
    Config c = configured(CONFIG_BOUNCER_ZNC);
    c.server_password[0] = 0;
    assert(config_validate(&c, error, sizeof error) == -1);
    strcpy(c.server_password_gpg_file, "/private/bouncer.gpg");
    assert(config_validate(&c, error, sizeof error) == 1);
    strcpy(c.password_file, "/private/upstream");
    assert(config_validate(&c, error, sizeof error) == -1);
    c = configured(CONFIG_BOUNCER_ZNC);
    strcpy(c.server_password_file, "/private/bouncer");
    assert(config_validate(&c, error, sizeof error) == -1);
    c = configured(CONFIG_BOUNCER_SOJU);
    c.password[0] = 0;
    assert(config_validate(&c, error, sizeof error) == -1);
    assert(strstr(error, "derived from [bouncer]") && !strstr(error, "requires auth_user"));
    strcpy(c.password_gpg_file, "/private/bouncer.gpg");
    assert(config_validate(&c, error, sizeof error) == 1);
    strcpy(c.server_password, "wrong-password-source");
    assert(config_validate(&c, error, sizeof error) == -1);
    c = configured(CONFIG_BOUNCER_ZNC);
    assert(config_bouncer_identity(&c, login, sizeof login) == 1);
    size_t max_password = 503u - strlen(login) - 1u;
    memset(c.server_password, 'P', max_password); c.server_password[max_password] = 0;
    assert(config_validate(&c, error, sizeof error) == 1);
    c.server_password[max_password] = 'P'; c.server_password[max_password + 1u] = 0;
    assert(config_validate(&c, error, sizeof error) == -1);
    assert(strstr(error, "PASS limit"));
    config_wipe(&c, sizeof c);
}

static void save_reload(void)
{
    char directory[] = "/tmp/irc-bouncer-config-XXXXXX", path[256], error[512];
    assert(mkdtemp(directory));
    assert(chmod(directory, 0700) == 0);
    snprintf(path, sizeof path, "%s/config.ini", directory);
    for (int minimal = 0; minimal <= 1; ++minimal) {
        for (int type = CONFIG_BOUNCER_ZNC; type <= CONFIG_BOUNCER_SOJU; ++type) {
            Config c = configured(type), loaded;
            c.minimal = minimal; c.bouncer_away = 1;
            strcpy(c.bouncer_client, "workstation");
            assert(config_save(&c, path, error, sizeof error) == 1);
            struct stat st; assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
            if (minimal) config_minimal_defaults(&loaded); else config_defaults(&loaded);
            assert(config_load(&loaded, path, error, sizeof error) == 1);
            assert(loaded.bouncer_type == type && loaded.bouncer_away == 1);
            assert(!strcmp(loaded.bouncer_user, c.bouncer_user));
            assert(!strcmp(loaded.bouncer_network, c.bouncer_network));
            assert(!strcmp(loaded.bouncer_client, c.bouncer_client));
            assert(!strcmp(loaded.password, c.password));
            assert(!strcmp(loaded.server_password, c.server_password));
            config_wipe(&c, sizeof c); config_wipe(&loaded, sizeof loaded);
        }
    }
    FILE *f = fopen(path, "w"); assert(f);
    assert(fputs("[connection]\nserver=irc.example.invalid\ntls=true\nnick=Alice\n", f) >= 0);
    assert(fclose(f) == 0);
    Config legacy; config_defaults(&legacy);
    assert(config_load(&legacy, path, error, sizeof error) == 1);
    assert(legacy.bouncer_type == CONFIG_BOUNCER_NONE && !legacy.bouncer_away);
    f = fopen(path, "w"); assert(f);
    assert(fputs("[bouncer]\ntype=unknown\n", f) >= 0); assert(fclose(f) == 0);
    Config unchanged = legacy;
    assert(config_load(&legacy, path, error, sizeof error) == -1);
    assert(!memcmp(&legacy, &unchanged, sizeof legacy));
    config_wipe(&legacy, sizeof legacy); config_wipe(&unchanged, sizeof unchanged);
    assert(unlink(path) == 0); assert(rmdir(directory) == 0);
}

int main(void)
{
    identity(); rejected_components(); credentials(); save_reload();
    puts("bouncer config: identity order, bounded components, credential isolation, TLS, PASS size, private round-trip and legacy defaults passed");
    return 0;
}
