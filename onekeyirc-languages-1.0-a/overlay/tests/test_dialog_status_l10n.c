#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    char out[4096], tiny[2];
    size_t checked=0;
    assert(irc_tr("Cancelled {messages} queued messages and {commands} automatic commands.") && *irc_tr("Cancelled {messages} queued messages and {commands} automatic commands."));
    { const IrcDisplayField f[] = {{"messages", "messages%n{literal}"}, {"commands", "commands%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Cancelled {messages} queued messages and {commands} automatic commands."), f, 2));
      assert(irc_display_render(out, sizeof out, "Cancelled {messages} queued messages and {commands} automatic commands.", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Cancelled {messages} queued messages and {commands} automatic commands.", f, 2) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("One in-flight message will finish.") && *irc_tr("One in-flight message will finish."));
    ++checked;
    assert(irc_tr("Watch string added; test-session change only; /save is disabled.") && *irc_tr("Watch string added; test-session change only; /save is disabled."));
    ++checked;
    assert(irc_tr("Watch string added; /save preserves it.") && *irc_tr("Watch string added; /save preserves it."));
    ++checked;
    assert(irc_tr("Watch string removed; test-session change only; /save is disabled.") && *irc_tr("Watch string removed; test-session change only; /save is disabled."));
    ++checked;
    assert(irc_tr("Watch string removed; /save preserves it.") && *irc_tr("Watch string removed; /save preserves it."));
    ++checked;
    assert(irc_tr("Nick mentions always match {nick}; {count} additional literal strings:") && *irc_tr("Nick mentions always match {nick}; {count} additional literal strings:"));
    { const IrcDisplayField f[] = {{"nick", "nick%n{literal}"}, {"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Nick mentions always match {nick}; {count} additional literal strings:"), f, 2));
      assert(irc_display_render(out, sizeof out, "Nick mentions always match {nick}; {count} additional literal strings:", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Nick mentions always match {nick}; {count} additional literal strings:", f, 2) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("Automatic command {number} was not sent: {error}. Remaining automatic commands cancelled.") && *irc_tr("Automatic command {number} was not sent: {error}. Remaining automatic commands cancelled."));
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"error", "error%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Automatic command {number} was not sent: {error}. Remaining automatic commands cancelled."), f, 2));
      assert(irc_display_render(out, sizeof out, "Automatic command {number} was not sent: {error}. Remaining automatic commands cancelled.", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Automatic command {number} was not sent: {error}. Remaining automatic commands cancelled.", f, 2) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("Automatic command {number} was not sent: the message limit changed. Remaining automatic commands cancelled.") && *irc_tr("Automatic command {number} was not sent: the message limit changed. Remaining automatic commands cancelled."));
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Automatic command {number} was not sent: the message limit changed. Remaining automatic commands cancelled."), f, 1));
      assert(irc_display_render(out, sizeof out, "Automatic command {number} was not sent: the message limit changed. Remaining automatic commands cancelled.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Automatic command {number} was not sent: the message limit changed. Remaining automatic commands cancelled.", f, 1) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("could not queue the command; check the connection and outgoing queue") && *irc_tr("could not queue the command; check the connection and outgoing queue"));
    ++checked;
    assert(irc_tr("invalid automatic command") && *irc_tr("invalid automatic command"));
    ++checked;
    assert(irc_tr("text exceeds the one-message byte limit; automatic messages are never split") && *irc_tr("text exceeds the one-message byte limit; automatic messages are never split"));
    ++checked;
    assert(irc_tr("no channel view is available") && *irc_tr("no channel view is available"));
    ++checked;
    printf("Dialogs and command status catalog: %zu entries PASS\n", checked);
    return 0;
}
