/* Real command dispatch/configuration, with the existing offline fixture. */
#define main prior_idle_commands_main
#include "test_idle_commands.c"
#undef main
#include "commands.h"

static void private_commands(void)
{
    static const char *const commands[] = {
        "/znc", "/ZNC SetPassword CONTROL_SECRET_CANARY",
        "/soju user update -password CONTROL_SECRET_CANARY",
        "/msg *status arbitrary CONTROL_SECRET_CANARY",
        "/message *sasl Set myuser CONTROL_SECRET_CANARY",
        "/privmsg BouncerServ arbitrary CONTROL_SECRET_CANARY",
        "/msg bOuNcErSeRv@host arbitrary CONTROL_SECRET_CANARY"
    };
    CommandHistory history;
    commands_history_init(&history);
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        assert(!idle_command_recordable(commands[i]));
        assert(!commands_history_add(&history, commands[i]));
    }
    assert(!history.count && !history.bytes);
    assert(commands_history_add(&history, "/msg ordinary hello"));
    assert(idle_command_recordable("/msg ordinary hello"));
    commands_history_free(&history);
}

static void reports_and_guards(void)
{
    Fixture f; init(&f, 1);
    assert(run(&f, "/help bouncers") == IDLE_COMMAND_DONE);
    assert(strstr(output, "one saved profile") && strstr(output, "/znc") && strstr(output, "/soju"));
    assert(run(&f, "/help znc") == IDLE_COMMAND_DONE && strstr(output, "*status"));
    assert(run(&f, "/help soju") == IDLE_COMMAND_DONE && strstr(output, "BouncerServ"));
    assert(run(&f, "/znc UNEXPECTED_CONTROL_CANARY") == IDLE_COMMAND_ERROR);
    assert(run(&f, "/soju UNEXPECTED_CONTROL_CANARY") == IDLE_COMMAND_ERROR);
    assert(!app(&f)->chat_count && !app(&f)->control_count);
    for (int type = CONFIG_BOUNCER_ZNC; type <= CONFIG_BOUNCER_SOJU; ++type) {
        Config next; char error[512]; app_config_effective(app(&f), &next);
        next.tls = 1; next.bouncer_type = type;
        strcpy(next.bouncer_user, "myuser"); strcpy(next.bouncer_network, "ircnow");
        strcpy(next.bouncer_client, "laptop"); next.bouncer_away = 0;
        strcpy(next.auth, type == CONFIG_BOUNCER_SOJU ? "sasl" : "none"); next.auth_user[0] = 0;
        config_wipe(next.password, sizeof next.password);
        config_wipe(next.server_password, sizeof next.server_password);
        strcpy(type == CONFIG_BOUNCER_SOJU ? next.password : next.server_password, "CONFIG_SECRET_CANARY");
        assert(app_config_apply(app(&f), &next, 0, error, sizeof error));
        assert(run(&f, "/settings") == IDLE_COMMAND_DONE);
        assert(strstr(output, type == CONFIG_BOUNCER_SOJU ? "myuser/ircnow@laptop" : "myuser@laptop/ircnow"));
        assert(strstr(output, "managed by bouncer") && !strstr(output, "CONFIG_SECRET_CANARY"));
        assert(run(&f, "/autoauth status") == IDLE_COMMAND_DONE);
        assert(strstr(output, "Bouncer:") && !strstr(output, "CONFIG_SECRET_CANARY"));
        Config before; app_config_effective(app(&f), &before);
        assert(run(&f, "/autoauth off") == IDLE_COMMAND_ERROR && strstr(output, "/panel"));
        Config after; app_config_effective(app(&f), &after);
        assert(!memcmp(&before, &after, sizeof before));
        assert(run(&f, "/autojoin") == IDLE_COMMAND_DONE);
        assert(strstr(output, "not replayed") && strstr(output, "/join"));
        const char *changes[] = {"/autojoin add #stale", "/autojoin remove #stale", "/autojoin clear"};
        for (size_t i = 0; i < sizeof changes / sizeof changes[0]; ++i) {
            assert(run(&f, changes[i]) == IDLE_COMMAND_ERROR);
            assert(strstr(output, "/join") && strstr(output, "/part") && strstr(output, "/panel"));
            app_config_effective(app(&f), &after);
            assert(!memcmp(&before, &after, sizeof before));
        }
        config_wipe(&next, sizeof next); config_wipe(&before, sizeof before); config_wipe(&after, sizeof after);
    }
    cleanup(&f);
}

int main(void)
{
    private_commands(); reports_and_guards();
    puts("bouncer command frontend: exact catalog/help, secret-free history, guarded direct mode, effective login reporting and staged credential preservation passed");
    return 0;
}
