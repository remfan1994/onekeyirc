/* Exercise the shared command reader, not only the formatting helper. */
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

int main(void)
{
    Fixture f;
    init(&f, 0);
    f.commands.translate = irc_tr;
    App *a = app(&f);
    strcpy(a->cfg.auth, "sasl");
    strcpy(a->cfg.auth_user, "literal%{account}");
    strcpy(a->cfg.password, "fixture-secret-MUST-NOT-APPEAR");
    strcpy(a->cfg.server_password_gpg_file, "/private/fixture-MUST-NOT-BE-READ.gpg");
    strcpy(a->cfg.background_away_message, "literal%{background}");
    strcpy(a->cfg.autoaway_message, "literal%{idle}");
    a->cfg.autoaway_minutes = 23;
    Config before = a->cfg;
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE);
    const IrcDisplayField auth[] = {{"mode", "sasl"}, {"account", a->cfg.auth_user}, {"source", irc_tr("plaintext config value (redacted)")}};
    const IrcDisplayField pass[] = {{"source", irc_tr("GPG file (redacted)")}};
    const IrcDisplayField away[] = {{"background", a->cfg.background_away_message}, {"idle", a->cfg.autoaway_message}};
    const IrcDisplayField minutes[] = {{"state", irc_tr(a->cfg.background_away ? "on" : "off")}, {"minutes", "23"}};
    expect_line("Auto-auth: {mode} | account: {account} | credential: {source}", auth, 3);
    expect_line("Server password: {source}", pass, 1);
    expect_line("Background away: {background} | Idle away: {idle}", away, 2);
    expect_line("Presence: background-away {state} | auto-away {minutes} minutes", minutes, 2);
    assert(!strstr(output, "MUST-NOT-APPEAR") && !strstr(output, "MUST-NOT-BE-READ"));
    assert(!memcmp(&before, &a->cfg, sizeof before));
    assert(run(&f, "/autoauth status") == IDLE_COMMAND_DONE);
    expect_line("Auto-auth: {mode} | account: {account} | credential: {source}", auth, 3);
    expect_line("Server password: {source}", pass, 1);
    const IrcDisplayField sasl[] = {{"status", irc_tr("not authenticated")}};
    expect_line("Required SASL: {status}", sasl, 1);
    const IrcDisplayField conn[] = {{"status", app_connection_label(a)}, {"registered", irc_tr("no")}};
    expect_line("Connection: {status} | registered: {registered}", conn, 2);
    assert(strstr(output, irc_tr("Authentication settings:")));
    assert(strstr(output, a->cfg.server_password_gpg_file) && !strstr(output, "MUST-NOT-APPEAR"));
    assert(!memcmp(&before, &a->cfg, sizeof before));
    assert(run(&f, "/autojoin list") == IDLE_COMMAND_DONE);
    const IrcDisplayField channels[] = {{"channels", a->cfg.channels}};
    expect_line("Running auto-join: {channels}", channels, 1);
    f.commands.translate = NULL;
    assert(run(&f, "/settings") == IDLE_COMMAND_DONE);
    assert(strstr(output, "Auto-auth: sasl | account: literal%{account} | credential: plaintext config value (redacted)"));
    assert(strstr(output, "Server password: GPG file (redacted)"));
    assert(!strstr(output, "MUST-NOT-APPEAR") && !strstr(output, "MUST-NOT-BE-READ"));
    config_wipe(&before, sizeof before);
    cleanup(&f);
    puts("Authentication, auto-join, presence reader localization: PASS");
    return 0;
}
