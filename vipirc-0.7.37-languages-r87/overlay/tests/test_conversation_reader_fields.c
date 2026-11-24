#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    char out[4096], tiny[2];
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Channels on server {number}:"), f, 1));
      assert(irc_display_render(out, sizeof out, "Channels on server {number}:", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Channels on server {number}:", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Queries on server {number}:"), f, 1));
      assert(irc_display_render(out, sizeof out, "Queries on server {number}:", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Queries on server {number}:", f, 1) && !tiny[0]);
    }
    assert(irc_tr(" (selected)") && *irc_tr(" (selected)"));
    assert(irc_tr("parted") && *irc_tr("parted"));
    { const IrcDisplayField f[] = {{"unread", "unread%n{literal}"}, {"mentions", "mentions%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("{unread} unread, {mentions} mentions"), f, 2));
      assert(irc_display_render(out, sizeof out, "{unread} unread, {mentions} mentions", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "{unread} unread, {mentions} mentions", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"selected", "selected%n{literal}"}, {"counts", "counts%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("  0: server{selected} | {counts}"), f, 2));
      assert(irc_display_render(out, sizeof out, "  0: server{selected} | {counts}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "  0: server{selected} | {counts}", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"name", "name%n{literal}"}, {"selected", "selected%n{literal}"}, {"counts", "counts%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("  {number}: {name}{selected} | {counts}"), f, 4));
      assert(irc_display_render(out, sizeof out, "  {number}: {name}{selected} | {counts}", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "  {number}: {name}{selected} | {counts}", f, 4) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"name", "name%n{literal}"}, {"counts", "counts%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("  server {number} | {name} | {counts}"), f, 3));
      assert(irc_display_render(out, sizeof out, "  server {number} | {name} | {counts}", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "  server {number} | {name} | {counts}", f, 3) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"channel", "channel%n{literal}"}, {"count", "count%n{literal}"}, {"incomplete", "incomplete%n{literal}"}, {"limit", "limit%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Users in {channel}: {count}{incomplete}{limit}"), f, 4));
      assert(irc_display_render(out, sizeof out, "Users in {channel}: {count}{incomplete}{limit}", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Users in {channel}: {count}{incomplete}{limit}", f, 4) && !tiny[0]);
    }
    assert(irc_tr(" (NAMES incomplete)") && *irc_tr(" (NAMES incomplete)"));
    assert(irc_tr(" (roster limit reached)") && *irc_tr(" (roster limit reached)"));
    { const IrcDisplayField f[] = {{"state", "state%n{literal}"}, {"count", "count%n{literal}"}, {"delay", "delay%n{literal}"}, {"interval", "interval%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Automatic commands: {state} | {count} | delay {delay} seconds | interval {interval} seconds"), f, 4));
      assert(irc_display_render(out, sizeof out, "Automatic commands: {state} | {count} | delay {delay} seconds | interval {interval} seconds", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Automatic commands: {state} | {count} | delay {delay} seconds | interval {interval} seconds", f, 4) && !tiny[0]);
    }
    assert(irc_tr(" [text hidden]") && *irc_tr(" [text hidden]"));
    { const IrcDisplayField f[] = {{"limit", "limit%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Automatic command list is full ({limit}); remove an entry first."), f, 1));
      assert(irc_display_render(out, sizeof out, "Automatic command list is full ({limit}); remove an entry first.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Automatic command list is full ({limit}); remove an entry first.", f, 1) && !tiny[0]);
    }
    assert(irc_tr("That automatic command number does not exist. Use /autocommands list.") && *irc_tr("That automatic command number does not exist. Use /autocommands list."));
    assert(irc_tr("Automatic command interval must be from 2 to 60 seconds.") && *irc_tr("Automatic command interval must be from 2 to 60 seconds."));
    assert(irc_tr("Automatic commands staged for the next connection; /save persists them. /cancel stops this connection's pending commands.") && *irc_tr("Automatic commands staged for the next connection; /save persists them. /cancel stops this connection's pending commands."));
    assert(irc_tr("Auto-connect changed for next startup. /save persists it; /connect acts now.") && *irc_tr("Auto-connect changed for next startup. /save persists it; /connect acts now."));
    assert(irc_tr("Auto-auth disabled for the next connection. Existing authentication is unchanged; /save persists it.") && *irc_tr("Auto-auth disabled for the next connection. Existing authentication is unchanged; /save persists it."));
    assert(irc_tr("SASL source staged for the next connection; credential not read. /save persists it.") && *irc_tr("SASL source staged for the next connection; credential not read. /save persists it."));
    assert(irc_tr("Auto-join list staged. /disconnect then /connect applies it; automatic retries keep the running list. /save persists it.") && *irc_tr("Auto-join list staged. /disconnect then /connect applies it; automatic retries keep the running list. /save persists it."));
    assert(irc_tr("Automatic command must be a single line of at most 511 UTF-8 bytes beginning with '/'.") && *irc_tr("Automatic command must be a single line of at most 511 UTF-8 bytes beginning with '/'."));
    assert(irc_tr("Automatic /msg requires one nickname or channel and nonempty text.") && *irc_tr("Automatic /msg requires one nickname or channel and nonempty text."));
    assert(irc_tr("Automatic /join requires one channel and at most one printable ASCII key.") && *irc_tr("Automatic /join requires one channel and at most one printable ASCII key."));
    assert(irc_tr("Automatic /nick and /whois require exactly one nickname.") && *irc_tr("Automatic /nick and /whois require exactly one nickname."));
    assert(irc_tr("Automatic /back takes no arguments.") && *irc_tr("Automatic /back takes no arguments."));
    assert(irc_tr("Automatic commands support /msg, /join, /nick, /away, /back and /whois only.") && *irc_tr("Automatic commands support /msg, /join, /nick, /away, /back and /whois only."));
    puts("Conversation and automatic command fields: PASS");
    return 0;
}
