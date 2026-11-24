#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    char out[4096], tiny[2];
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"counts", "counts%n{literal}"}, {"order", "order%n{literal}"}, {"receiving", "receiving%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Network channel directory on server {number}: {counts} | {order}{receiving}"), f, 4));
      assert(irc_display_render(out, sizeof out, "Network channel directory on server {number}: {counts} | {order}{receiving}", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Network channel directory on server {number}: {counts} | {order}{receiving}", f, 4) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"visible", "visible%n{literal}"}, {"cached", "cached%n{literal}"}, {"matched", "matched%n{literal}"}, {"received", "received%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("showing {visible}, {cached} cached, {matched} matched, {received} received"), f, 4));
      assert(irc_display_render(out, sizeof out, "showing {visible}, {cached} cached, {matched} matched, {received} received", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "showing {visible}, {cached} cached, {matched} matched, {received} received", f, 4) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"sort", "sort%n{literal}"}, {"direction", "direction%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("sort {sort} {direction}"), f, 2));
      assert(irc_display_render(out, sizeof out, "sort {sort} {direction}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "sort {sort} {direction}", f, 2) && !tiny[0]);
    }
    assert(irc_tr(" | still receiving") && *irc_tr(" | still receiving"));
    { const IrcDisplayField f[] = {{"channel", "channel%n{literal}"}, {"count", "count%n{literal}"}, {"topic", "topic%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("  {channel} | {count} users | {topic}"), f, 3));
      assert(irc_display_render(out, sizeof out, "  {channel} | {count} users | {topic}", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "  {channel} | {count} users | {topic}", f, 3) && !tiny[0]);
    }
    assert(irc_tr("(no topic)") && *irc_tr("(no topic)"));
    { const IrcDisplayField f[] = {{"visible", "visible%n{literal}"}, {"cached", "cached%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Display limited to {visible} of {cached} cached matches."), f, 2));
      assert(irc_display_render(out, sizeof out, "Display limited to {visible} of {cached} cached matches.", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Display limited to {visible} of {cached} cached matches.", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"limit", "limit%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Channel cache reached its hard safety limit of {limit} entries."), f, 1));
      assert(irc_display_render(out, sizeof out, "Channel cache reached its hard safety limit of {limit} entries.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Channel cache reached its hard safety limit of {limit} entries.", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"endpoint", "endpoint%n{literal}"}, {"selected", "selected%n{literal}"}, {"details", "details%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("  {number}: {endpoint}{selected} | {details}"), f, 4));
      assert(irc_display_render(out, sizeof out, "  {number}: {endpoint}{selected} | {details}", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "  {number}: {endpoint}{selected} | {details}", f, 4) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"nick", "nick%n{literal}"}, {"status", "status%n{literal}"}, {"state", "state%n{literal}"}, {"saved", "saved%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("nick {nick} | {status} | auto-connect {state} | {saved}"), f, 4));
      assert(irc_display_render(out, sizeof out, "nick {nick} | {status} | auto-connect {state} | {saved}", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "nick {nick} | {status} | auto-connect {state} | {saved}", f, 4) && !tiny[0]);
    }
    assert(irc_tr("(default)") && *irc_tr("(default)"));
    assert(irc_tr("profile error") && *irc_tr("profile error"));
    assert(irc_tr("Join requests use the running list after registration. Channels still not joined may be pending or rejected; see this server's replies.") && *irc_tr("Join requests use the running list after registration. Channels still not joined may be pending or rejected; see this server's replies."));
    assert(irc_tr("Pending channels do not change this connection or automatic retries. /disconnect then /connect applies them; /save persists them.") && *irc_tr("Pending channels do not change this connection or automatic retries. /disconnect then /connect applies them; /save persists them."));
    assert(irc_tr("Use /join for an immediate join on a registered connection. /autojoin add/remove/clear prepares the next explicit /connect.") && *irc_tr("Use /join for an immediate join on a registered connection. /autojoin add/remove/clear prepares the next explicit /connect."));
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Server {number} was not saved: profile could not be loaded."), f, 1));
      assert(irc_display_render(out, sizeof out, "Server {number} was not saved: profile could not be loaded.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Server {number} was not saved: profile could not be loaded.", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"error", "error%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Server {number} save did not complete: {error}"), f, 2));
      assert(irc_display_render(out, sizeof out, "Server {number} save did not complete: {error}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Server {number} save did not complete: {error}", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"preferences", "preferences%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Saved server {number}{preferences}."), f, 2));
      assert(irc_display_render(out, sizeof out, "Saved server {number}{preferences}.", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Saved server {number}{preferences}.", f, 2) && !tiny[0]);
    }
    assert(irc_tr(" (including application preferences)") && *irc_tr(" (including application preferences)"));
    { const IrcDisplayField f[] = {{"done", "done%n{literal}"}, {"failed", "failed%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Saved {done} profile(s); {failed} failed. Each successful file remains saved."), f, 2));
      assert(irc_display_render(out, sizeof out, "Saved {done} profile(s); {failed} failed. Each successful file remains saved.", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Saved {done} profile(s); {failed} failed. Each successful file remains saved.", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"saved", "saved%n{literal}"}, {"failed", "failed%n{literal}"}, {"state", "state%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Configuration save incomplete: {saved} server(s) saved; {failed} failed. Selected default {state}. Successful writes remain saved."), f, 3));
      assert(irc_display_render(out, sizeof out, "Configuration save incomplete: {saved} server(s) saved; {failed} failed. Selected default {state}. Successful writes remain saved.", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Configuration save incomplete: {saved} server(s) saved; {failed} failed. Selected default {state}. Successful writes remain saved.", f, 3) && !tiny[0]);
    }
    assert(irc_tr("remembered") && *irc_tr("remembered"));
    assert(irc_tr("not confirmed") && *irc_tr("not confirmed"));
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Saved configuration for {count} server(s). /connect will reuse the selected connection next time."), f, 1));
      assert(irc_display_render(out, sizeof out, "Saved configuration for {count} server(s). /connect will reuse the selected connection next time.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Saved configuration for {count} server(s). /connect will reuse the selected connection next time.", f, 1) && !tiny[0]);
    }
    assert(irc_tr("OFFLINE TEST MODE: /saveconf does not write configuration.") && *irc_tr("OFFLINE TEST MODE: /saveconf does not write configuration."));
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"host", "host%n{literal}"}, {"status", "status%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Selected server {number}: {host} | {status}"), f, 3));
      assert(irc_display_render(out, sizeof out, "Selected server {number}: {host} | {status}", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Selected server {number}: {host} | {status}", f, 3) && !tiny[0]);
    }
    assert(irc_tr("The saved default connection is unavailable. Use /connect HOST or /server NUMBER to choose one explicitly.") && *irc_tr("The saved default connection is unavailable. Use /connect HOST or /server NUMBER to choose one explicitly."));
    puts("Server and directory fields: PASS");
    return 0;
}
