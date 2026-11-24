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
    assert(irc_tr("This text needs {count} IRC messages.") && *irc_tr("This text needs {count} IRC messages."));
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("This text needs {count} IRC messages."), f, 1));
      assert(irc_display_render(out, sizeof out, "This text needs {count} IRC messages.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "This text needs {count} IRC messages.", f, 1) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("Send all to {target}? [y/N]") && *irc_tr("Send all to {target}? [y/N]"));
    { const IrcDisplayField f[] = {{"target", "target%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Send all to {target}? [y/N]"), f, 1));
      assert(irc_display_render(out, sizeof out, "Send all to {target}? [y/N]", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Send all to {target}? [y/N]", f, 1) && !tiny[0]);
      assert(strstr(out, "[y/N]"));
    }
    ++checked;
    assert(irc_tr("Close {target}?") && *irc_tr("Close {target}?"));
    { const IrcDisplayField f[] = {{"target", "target%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Close {target}?"), f, 1));
      assert(irc_display_render(out, sizeof out, "Close {target}?", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Close {target}?", f, 1) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("Channel {number}: {name}") && *irc_tr("Channel {number}: {name}"));
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"name", "name%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Channel {number}: {name}"), f, 2));
      assert(irc_display_render(out, sizeof out, "Channel {number}: {name}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Channel {number}: {name}", f, 2) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("Query: {name}") && *irc_tr("Query: {name}"));
    { const IrcDisplayField f[] = {{"name", "name%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Query: {name}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Query: {name}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Query: {name}", f, 1) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("Cleared mentions across all servers: {count}.") && *irc_tr("Cleared mentions across all servers: {count}."));
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Cleared mentions across all servers: {count}."), f, 1));
      assert(irc_display_render(out, sizeof out, "Cleared mentions across all servers: {count}.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Cleared mentions across all servers: {count}.", f, 1) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("Mentions ({count})") && *irc_tr("Mentions ({count})"));
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Mentions ({count})"), f, 1));
      assert(irc_display_render(out, sizeof out, "Mentions ({count})", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Mentions ({count})", f, 1) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("Unread conversations ({count})") && *irc_tr("Unread conversations ({count})"));
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Unread conversations ({count})"), f, 1));
      assert(irc_display_render(out, sizeof out, "Unread conversations ({count})", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Unread conversations ({count})", f, 1) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("Mentions and unread | All servers") && *irc_tr("Mentions and unread | All servers"));
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
    assert(irc_tr("Server {number}: {host}") && *irc_tr("Server {number}: {host}"));
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"host", "host%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Server {number}: {host}"), f, 2));
      assert(irc_display_render(out, sizeof out, "Server {number}: {host}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Server {number}: {host}", f, 2) && !tiny[0]);
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
