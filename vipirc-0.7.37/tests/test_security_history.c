/* Compare accepted message plans with both frontend history privacy gates.
 * The offline simulator prevents any credential or network operation. */
#define main legacy_idle_commands_test_main
#include "test_idle_commands.c"
#undef main
#include "commands.h"

static void split_plan_privacy(Fixture *f)
{
    static const char *const suffixes[] = {
        "IDENTIFY SPLIT_SECRET_CANARY", ": :iDeNtIfY 123 SPLIT_SECRET_CANARY",
        "LOGIN 123 SPLIT_SECRET_CANARY", "SET PASSWORD SPLIT_SECRET_CANARY",
        "SASET 123 PASSWORD SPLIT_SECRET_CANARY"
    };
    char command[APP_TEXT + 1];
    const char prefix[] = "/msg CustomAuthBot ";
    size_t at = sizeof prefix - 1, budget = app_budget(app(f), "CustomAuthBot", 0);
    assert(budget > 8 && at + budget + 128 < sizeof command);
    memcpy(command, prefix, at);
    memset(command + at, 'x', budget);
    memcpy(command + at, "HELP ", 5);
    for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; ++i) {
        strcpy(command + at + budget, suffixes[i]);
        assert(run(f, command) == IDLE_COMMAND_SEND && f->plan.count == 2);
        assert(f->plan.chunks[1].offset == budget);
        assert(!strcmp(f->plan.text + budget, suffixes[i]));
        /* Syntax-only filters cannot see a later wire command. The actual
         * validated plan supplies the additional privacy boundary. */
        assert(idle_command_recordable(command));
        assert(!app_plan_history_safe(&f->plan));
    }
    strcpy(command + at + budget, "ordinary second fragment");
    assert(run(f, command) == IDLE_COMMAND_SEND && f->plan.count == 2);
    assert(app_plan_history_safe(&f->plan));
    SendPlan ordinary = f->plan;
    f->plan.chunks[1].offset = SIZE_MAX;
    assert(!app_plan_history_safe(&f->plan));
    f->plan = ordinary; f->plan.count = APP_QUEUE + 1u;
    assert(!app_plan_history_safe(&f->plan));
    f->plan = ordinary; f->plan.budget = 0;
    assert(!app_plan_history_safe(&f->plan));
    f->plan = ordinary; memset(f->plan.text, 'x', sizeof f->plan.text);
    assert(!app_plan_history_safe(&f->plan));
    f->plan = ordinary; memset(f->plan.target, 'x', sizeof f->plan.target);
    assert(!app_plan_history_safe(&f->plan));
    assert(!app_plan_history_safe(NULL));
    config_wipe(&ordinary, sizeof ordinary);
    config_wipe(command, sizeof command);
}

int main(void)
{
    static const char *const sensitive[] = {
        "/msg CustomAuthBot ::IDENTIFY HISTORY_SECRET_CANARY",
        "/msg CustomAuthBot : : IDENTIFY 123 HISTORY_SECRET_CANARY",
        "/MsG CustomAuthBot :::iDeNtIfY account HISTORY_SECRET_CANARY",
        "/message CustomAuthBot : :LOGIN 123 HISTORY_SECRET_CANARY",
        "/privmsg CustomAuthBot : : :AUTH 123 HISTORY_SECRET_CANARY",
        "/msg CustomAuthBot ::REGISTER HISTORY_SECRET_CANARY mail@example.invalid",
        "/msg CustomAuthBot : : SET PASSWORD HISTORY_SECRET_CANARY",
        "/msg CustomAuthBot :::SASET 123 PASSWORD HISTORY_SECRET_CANARY",
        "/msg CustomAuthBot : :OPER 123 HISTORY_SECRET_CANARY",
        "/msg NickServ : :IDENTIFY HISTORY_SECRET_CANARY",
    };
    static const char *const ordinary[] = {
        "/msg CustomAuthBot hello",
        "/msg CustomAuthBot : : ordinary text",
        "/message Alice IDENTIFYING a problem",
        "/privmsg Alice : : REGISTERED 123",
    };
    Fixture f; init(&f, 1);
    assert(run(&f, "/connect") == IDLE_COMMAND_DONE);
    pump(app(&f));
    assert(app(&f)->registered && app(&f)->sim && !app(&f)->net);
    for (size_t i = 0; i < sizeof sensitive / sizeof sensitive[0]; ++i) {
        CommandHistory history;
        commands_history_init(&history);
        size_t queued = app(&f)->chat_count;
        assert(run(&f, sensitive[i]) == IDLE_COMMAND_SEND);
        assert(strstr(f.plan.text, "HISTORY_SECRET_CANARY") && app(&f)->chat_count == queued);
        assert(!app_plan_history_safe(&f.plan));
        assert(!idle_command_recordable(sensitive[i]));
        assert(commands_history_add(&history, sensitive[i]) == 0 && !history.count);
        commands_history_free(&history);
    }
    /* Invalid numeric message recipients must not bypass the privacy gate;
     * numeric server-selection commands remain ordinary recallable commands. */
    assert(run(&f, "/msg 123 : :IDENTIFY HISTORY_SECRET_CANARY") == IDLE_COMMAND_ERROR);
    assert(!idle_command_recordable("/msg 123 : :IDENTIFY HISTORY_SECRET_CANARY"));
    assert(idle_command_recordable("/server 1") && idle_command_recordable("/connect 1"));
    for (size_t i = 0; i < sizeof ordinary / sizeof ordinary[0]; ++i) {
        CommandHistory history;
        commands_history_init(&history);
        assert(run(&f, ordinary[i]) == IDLE_COMMAND_SEND);
        assert(app_plan_history_safe(&f.plan));
        assert(idle_command_recordable(ordinary[i]));
        assert(commands_history_add(&history, ordinary[i]) == 1 && history.count == 1);
        commands_history_free(&history);
    }
    split_plan_privacy(&f);
    cleanup(&f);
    puts("history security: credential prefix/case/alias/account variants and actual split-command slices are private; ordinary and numeric selection controls remain recallable; malformed plans fail closed");
    return 0;
}
