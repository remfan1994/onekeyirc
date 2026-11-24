#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#define main legacy_idle_commands_main
#include "test_idle_commands.c"
#undef main

static void expect_line(const char *key, const IrcDisplayField *f, size_t n)
{
    char expected[2048];
    assert(irc_display_fields_valid(irc_tr(key), f, n));
    assert(irc_display_render(expected, sizeof expected, key, f, n));
    assert(strstr(output, expected));
}

/* Independently construct the expected escaped pieces and exact ranges. */
static void expect_path(const char *label, const char *path)
{
    size_t length = strlen(path), offset = 0;
    while (offset < length) {
        char part[385], range[64], total[32];
        size_t used = 0, start = offset;
        while (offset < length) {
            unsigned char b = (unsigned char)path[offset];
            char piece[5];
            if (b == '\\') strcpy(piece, "\\\\");
            else if (b < 32 || b >= 127) snprintf(piece, sizeof piece, "\\x%02X", b);
            else { piece[0] = (char)b; piece[1] = 0; }
            size_t n = strlen(piece);
            if (used + n >= sizeof part) break;
            memcpy(part + used, piece, n); used += n; ++offset;
        }
        part[used] = 0;
        snprintf(range, sizeof range, "%zu-%zu", start + 1, offset);
        snprintf(total, sizeof total, "%zu", length);
        const IrcDisplayField fields[] = {{"label", label}, {"range", range}, {"total", total}, {"path", part}};
        expect_line("{label} [bytes {range} of {total}]: {path}", fields, 4);
    }
}

int main(void)
{
    Fixture f; init(&f, 1); App *a = app(&f);
    f.commands.translate = irc_tr;
    strcpy(a->cfg.auth, "sasl");
    strcpy(a->cfg.auth_user, "literal-account");
    strcpy(a->cfg.password_file, "/SOURCE_SASL_PATH/");
    size_t start = strlen(a->cfg.password_file);
    for (size_t i = start; i + 1 < sizeof a->cfg.password_file; ++i)
        a->cfg.password_file[i] = (char)('a' + i % 26);
    a->cfg.password_file[sizeof a->cfg.password_file - 1] = 0;
    a->cfg.password_file[383] = (char)0xc3; a->cfg.password_file[384] = (char)0xa9;
    a->cfg.password_file[700] = '\\';
    memcpy(a->cfg.server_password_gpg_file, a->cfg.password_file, sizeof a->cfg.server_password_gpg_file);
    a->cfg.server_password_gpg_file[8] = 'G';
    App before = *a;
    assert(run(&f, "/autoauth status") == IDLE_COMMAND_DONE);
    const char *label = irc_tr("SASL password file");
    expect_path(label, a->cfg.password_file);
    expect_path(irc_tr("Server-password GPG file"), a->cfg.server_password_gpg_file);
    const IrcDisplayField escaped[] = {{"label", label}, {"backslash", "\\\\"}, {"hex", "\\xHH"}};
    expect_line("{label} display uses {backslash} for a backslash and {hex} for other escaped bytes.", escaped, 3);
    assert(!memcmp(&before, a, sizeof before));
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE);
    assert(strstr(output, irc_tr("Application preferences:")));
    const IrcDisplayField names[] = {{"nick", a->cfg.nick}, {"user", a->cfg.username}, {"real", a->cfg.realname}};
    expect_line("Nickname: {nick} | username: {user} | real name: {real}", names, 3);
    assert(!strstr(output, "/SOURCE_"));
    assert(run(&f, "/summary") == IDLE_COMMAND_DONE && !strstr(output, "/SOURCE_"));
    Config next; app_config_effective(a, &next);
    next.password_file[0] = next.server_password_gpg_file[0] = 0;
    strcpy(next.password, "SASL_CONTENT_NEVER_SHOWN");
    strcpy(next.server_password_file, "/NEXT_PATH%{literal}");
    char error[512];
    assert(app_config_apply(a, &next, 0, error, sizeof error));
    before = *a;
    assert(run(&f, "/autoauth status") == IDLE_COMMAND_DONE);
    char current[512], pending[512], line[1024];
    const IrcDisplayField cur[] = {{"label", label}};
    const IrcDisplayField pen[] = {{"label", irc_tr("Server-password file")}};
    assert(irc_display_render(current, sizeof current, "Current {label}", cur, 1));
    assert(irc_display_render(pending, sizeof pending, "Next {label}", pen, 1));
    expect_path(current, a->cfg.password_file);
    snprintf(line, sizeof line, "%s: %s", pending, next.server_password_file);
    assert(strstr(output, line));
    assert(!strstr(output, "SASL_CONTENT_NEVER_SHOWN"));
    assert(!memcmp(&before, a, sizeof before));
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE);
    assert(!strstr(output, "/NEXT_PATH") && !strstr(output, "/SOURCE_") && !strstr(output, "SASL_CONTENT_NEVER_SHOWN"));
    f.commands.translate = NULL;
    assert(run(&f, "/autoauth status") == IDLE_COMMAND_DONE);
    char recovered[CONFIG_PATH_MAX];
    recover_path("Current SASL password file", recovered, sizeof recovered);
    assert(!strcmp(recovered, a->cfg.password_file));
    config_wipe(&next, sizeof next); config_wipe(recovered, sizeof recovered);
    cleanup(&f);
    puts("Credential path and settings localization: PASS");
    return 0;
}
