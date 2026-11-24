#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    char out[4096], tiny[2];
    { const IrcDisplayField f[] = {{"nick", "nick%n{literal}"}, {"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Watch strings: nickname {nick} plus {count} literal terms"), f, 2));
      assert(irc_display_render(out, sizeof out, "Watch strings: nickname {nick} plus {count} literal terms", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Watch strings: nickname {nick} plus {count} literal terms", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Ignore masks: {count}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Ignore masks: {count}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Ignore masks: {count}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Custom key bindings: {count} (application-wide; none assigned by default)"), f, 1));
      assert(irc_display_render(out, sizeof out, "Custom key bindings: {count} (application-wide; none assigned by default)", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Custom key bindings: {count} (application-wide; none assigned by default)", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"error", "error%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Bindings unchanged: {error}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Bindings unchanged: {error}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Bindings unchanged: {error}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"key", "key%n{literal}"}, {"action", "action%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Bound {key} to {action} for all servers. /save input saves this preference."), f, 2));
      assert(irc_display_render(out, sizeof out, "Bound {key} to {action} for all servers. /save input saves this preference.", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Bound {key} to {action} for all servers. /save input saves this preference.", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"key", "key%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Removed {key} custom binding. /save input saves this preference."), f, 1));
      assert(irc_display_render(out, sizeof out, "Removed {key} custom binding. /save input saves this preference.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Removed {key} custom binding. /save input saves this preference.", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"server", "server%n{literal}"}, {"port", "port%n{literal}"}, {"nick", "nick%n{literal}"}, {"status", "status%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Selected {server}:{port} as {nick} ({status})."), f, 4));
      assert(irc_display_render(out, sizeof out, "Selected {server}:{port} as {nick} ({status}).", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Selected {server}:{port} as {nick} ({status}).", f, 4) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"server", "server%n{literal}"}, {"port", "port%n{literal}"}, {"nick", "nick%n{literal}"}, {"transport", "transport%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Connecting to {server}:{port} as {nick}{transport}."), f, 4));
      assert(irc_display_render(out, sizeof out, "Connecting to {server}:{port} as {nick}{transport}.", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Connecting to {server}:{port} as {nick}{transport}.", f, 4) && !tiny[0]);
    }
    assert(irc_tr(" (offline simulation)") && *irc_tr(" (offline simulation)"));
    assert(irc_tr(" with verified TLS") && *irc_tr(" with verified TLS"));
    assert(irc_tr(" with explicit plaintext transport") && *irc_tr(" with explicit plaintext transport"));
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"select", "select%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Added server {number} with verified TLS and autoconnect off. /server {select} selects it; /connect then connects; /save persists."), f, 2));
      assert(irc_display_render(out, sizeof out, "Added server {number} with verified TLS and autoconnect off. /server {select} selects it; /connect then connects; /save persists.", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Added server {number} with verified TLS and autoconnect off. /server {select} selects it; /connect then connects; /save persists.", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"action", "action%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Server {number}: {action} requested."), f, 2));
      assert(irc_display_render(out, sizeof out, "Server {number}: {action} requested.", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Server {number}: {action} requested.", f, 2) && !tiny[0]);
    }
    assert(irc_tr("Key") && *irc_tr("Key"));
    assert(irc_tr("Action") && *irc_tr("Action"));
    assert(irc_tr("Custom key actions (local program functions):") && *irc_tr("Custom key actions (local program functions):"));
    assert(irc_tr("Only main chat/command input accepts bindings; paste and modal screens retain their controls.") && *irc_tr("Only main chat/command input accepts bindings; paste and modal screens retain their controls."));
    assert(irc_tr("/bind KEY ACTION assigns; /unbind KEY|all removes; /save input saves the list.") && *irc_tr("/bind KEY ACTION assigns; /unbind KEY|all removes; /save input saves the list."));
    assert(irc_tr("Supported keys: F1..F12; Ctrl-A..Ctrl-Z; Alt-a..Alt-z; Alt-0..Alt-9.") && *irc_tr("Supported keys: F1..F12; Ctrl-A..Ctrl-Z; Alt-a..Alt-z; Alt-0..Alt-9."));
    assert(irc_tr("Aliases: Ctrl-H = Backspace, Ctrl-I = Tab, Ctrl-J/Ctrl-M = Enter, Ctrl-[ = Escape.") && *irc_tr("Aliases: Ctrl-H = Backspace, Ctrl-I = Tab, Ctrl-J/Ctrl-M = Enter, Ctrl-[ = Escape."));
    assert(irc_tr("Alt letters are unshifted lowercase. Terminal signals and terminal/multiplexer shortcuts take precedence.") && *irc_tr("Alt letters are unshifted lowercase. Terminal signals and terminal/multiplexer shortcuts take precedence."));
    assert(irc_tr("Focus the chat composer") && *irc_tr("Focus the chat composer"));
    assert(irc_tr("Open the command prompt") && *irc_tr("Open the command prompt"));
    assert(irc_tr("Open or list custom key bindings") && *irc_tr("Open or list custom key bindings"));
    assert(irc_tr("Open help for the current screen") && *irc_tr("Open help for the current screen"));
    assert(irc_tr("Read the full manual") && *irc_tr("Read the full manual"));
    assert(irc_tr("Review all server activity") && *irc_tr("Review all server activity"));
    assert(irc_tr("List server connections") && *irc_tr("List server connections"));
    assert(irc_tr("List channels on this server") && *irc_tr("List channels on this server"));
    assert(irc_tr("List private conversations") && *irc_tr("List private conversations"));
    assert(irc_tr("List unread conversations") && *irc_tr("List unread conversations"));
    assert(irc_tr("Review retained mentions") && *irc_tr("Review retained mentions"));
    assert(irc_tr("Review file transfers") && *irc_tr("Review file transfers"));
    assert(irc_tr("Open application settings") && *irc_tr("Open application settings"));
    assert(irc_tr("Open connection setup") && *irc_tr("Open connection setup"));
    assert(irc_tr("Select the next conversation") && *irc_tr("Select the next conversation"));
    assert(irc_tr("Select the previous conversation") && *irc_tr("Select the previous conversation"));
    assert(irc_tr("Clear the current input draft") && *irc_tr("Clear the current input draft"));
    assert(irc_tr("Cancel this server's unsent queue") && *irc_tr("Cancel this server's unsent queue"));
    assert(irc_tr("Detach this terminal") && *irc_tr("Detach this terminal"));
    assert(irc_tr("Quit all server sessions") && *irc_tr("Quit all server sessions"));
    puts("Hotkey and watch fields: PASS");
    return 0;
}
