#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    char out[4096], tiny[2];
    { const IrcDisplayField f[] = {{"nick", "nick%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Registering as {nick}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Registering as {nick}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Registering as {nick}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"command", "command%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Usage: /{command} nickname|query-number"), f, 1));
      assert(irc_display_render(out, sizeof out, "Usage: /{command} nickname|query-number", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Usage: /{command} nickname|query-number", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"nick", "nick%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Query with {nick}; type a message to send."), f, 1));
      assert(irc_display_render(out, sizeof out, "Query with {nick}; type a message to send.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Query with {nick}; type a message to send.", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"nick", "nick%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Nickname set to {nick}; /connect to use it."), f, 1));
      assert(irc_display_render(out, sizeof out, "Nickname set to {nick}; /connect to use it.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Nickname set to {nick}; /connect to use it.", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"command", "command%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Unknown command /{command}; /help lists supported commands."), f, 1));
      assert(irc_display_render(out, sizeof out, "Unknown command /{command}; /help lists supported commands.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Unknown command /{command}; /help lists supported commands.", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"command", "command%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Usage: /{command}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Usage: /{command}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Usage: /{command}", f, 1) && !tiny[0]);
    }
    puts("Connection and directory localization: PASS");
    return 0;
}
