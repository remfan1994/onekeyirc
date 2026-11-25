#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    char out[4096], tiny[2];
    { const IrcDisplayField f[] = {{"type", "type%n{literal}"}, {"login", "login%n{literal}"}, {"away", "away%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Bouncer: {type} | login: {login} | automatic away: {away}"), f, 3));
      assert(irc_display_render(out, sizeof out, "Bouncer: {type} | login: {login} | automatic away: {away}", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Bouncer: {type} | login: {login} | automatic away: {away}", f, 3) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"source", "source%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Bouncer credential: {source}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Bouncer credential: {source}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Bouncer credential: {source}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"mode", "mode%n{literal}"}, {"account", "account%n{literal}"}, {"source", "source%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Auto-auth: {mode} | account: {account} | credential: {source}"), f, 3));
      assert(irc_display_render(out, sizeof out, "Auto-auth: {mode} | account: {account} | credential: {source}", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Auto-auth: {mode} | account: {account} | credential: {source}", f, 3) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"source", "source%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Server password: {source}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Server password: {source}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Server password: {source}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"source", "source%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Next server password: {source}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Next server password: {source}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Next server password: {source}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}, {"host", "host%n{literal}"}, {"port", "port%n{literal}"}, {"transport", "transport%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Server {number}: {host}:{port} ({transport})"), f, 4));
      assert(irc_display_render(out, sizeof out, "Server {number}: {host}:{port} ({transport})", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Server {number}: {host}:{port} ({transport})", f, 4) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"status", "status%n{literal}"}, {"registered", "registered%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Connection: {status} | registered: {registered}"), f, 2));
      assert(irc_display_render(out, sizeof out, "Connection: {status} | registered: {registered}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Connection: {status} | registered: {registered}", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"status", "status%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Required SASL: {status}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Required SASL: {status}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Required SASL: {status}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"host", "host%n{literal}"}, {"port", "port%n{literal}"}, {"transport", "transport%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Next explicit /connect endpoint: {host}:{port} ({transport})"), f, 3));
      assert(irc_display_render(out, sizeof out, "Next explicit /connect endpoint: {host}:{port} ({transport})", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Next explicit /connect endpoint: {host}:{port} ({transport})", f, 3) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"background", "background%n{literal}"}, {"idle", "idle%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Background away: {background} | Idle away: {idle}"), f, 2));
      assert(irc_display_render(out, sizeof out, "Background away: {background} | Idle away: {idle}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Background away: {background} | Idle away: {idle}", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"channels", "channels%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Running auto-join: {channels}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Running auto-join: {channels}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Running auto-join: {channels}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"channels", "channels%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Pending auto-join for next explicit /connect: {channels}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Pending auto-join for next explicit /connect: {channels}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Pending auto-join for next explicit /connect: {channels}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"channel", "channel%n{literal}"}, {"status", "status%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Channel {channel}: {status}"), f, 2));
      assert(irc_display_render(out, sizeof out, "Channel {channel}: {status}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Channel {channel}: {status}", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"state", "state%n{literal}"}, {"minutes", "minutes%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Presence: background-away {state} | auto-away {minutes} minute"), f, 2));
      assert(irc_display_render(out, sizeof out, "Presence: background-away {state} | auto-away {minutes} minute", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Presence: background-away {state} | auto-away {minutes} minute", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"state", "state%n{literal}"}, {"minutes", "minutes%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Presence: background-away {state} | auto-away {minutes} minutes"), f, 2));
      assert(irc_display_render(out, sizeof out, "Presence: background-away {state} | auto-away {minutes} minutes", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Presence: background-away {state} | auto-away {minutes} minutes", f, 2) && !tiny[0]);
    }
    puts("Authentication and presence localization: PASS");
    return 0;
}
