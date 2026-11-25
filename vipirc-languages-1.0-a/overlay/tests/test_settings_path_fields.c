#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    char out[4096], tiny[2];
    assert(irc_tr("SASL password file") && *irc_tr("SASL password file"));
    assert(irc_tr("SASL GPG file") && *irc_tr("SASL GPG file"));
    assert(irc_tr("Server-password file") && *irc_tr("Server-password file"));
    assert(irc_tr("Server-password GPG file") && *irc_tr("Server-password GPG file"));
    { const IrcDisplayField f[] = {{"label", "label%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Current {label}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Current {label}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Current {label}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"label", "label%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Next {label}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Next {label}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Next {label}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"label", "label%n{literal}"}, {"backslash", "backslash%n{literal}"}, {"hex", "hex%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("{label} display uses {backslash} for a backslash and {hex} for other escaped bytes."), f, 3));
      assert(irc_display_render(out, sizeof out, "{label} display uses {backslash} for a backslash and {hex} for other escaped bytes.", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "{label} display uses {backslash} for a backslash and {hex} for other escaped bytes.", f, 3) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"label", "label%n{literal}"}, {"range", "range%n{literal}"}, {"total", "total%n{literal}"}, {"path", "path%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("{label} [bytes {range} of {total}]: {path}"), f, 4));
      assert(irc_display_render(out, sizeof out, "{label} [bytes {range} of {total}]: {path}", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "{label} [bytes {range} of {total}]: {path}", f, 4) && !tiny[0]);
    }
    assert(irc_tr("Application preferences:") && *irc_tr("Application preferences:"));
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}, {"mode", "mode%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("History lines: {count} | send mode: {mode}"), f, 2));
      assert(irc_display_render(out, sizeof out, "History lines: {count} | send mode: {mode}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "History lines: {count} | send mode: {mode}", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"format", "format%n{literal}"}, {"queue", "queue%n{literal}"}, {"messages", "messages%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Output format: {format} | queue notices: {queue} | program messages: {messages}"), f, 3));
      assert(irc_display_render(out, sizeof out, "Output format: {format} | queue notices: {queue} | program messages: {messages}", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Output format: {format} | queue notices: {queue} | program messages: {messages}", f, 3) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"logging", "logging%n{literal}"}, {"session", "session%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Persistent logging: {logging} | single-session mode: {session}"), f, 2));
      assert(irc_display_render(out, sizeof out, "Persistent logging: {logging} | single-session mode: {session}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Persistent logging: {logging} | single-session mode: {session}", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"interface", "interface%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Default interface: idle (fixed in onekeyirc; saved preference: {interface})"), f, 1));
      assert(irc_display_render(out, sizeof out, "Default interface: idle (fixed in onekeyirc; saved preference: {interface})", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Default interface: idle (fixed in onekeyirc; saved preference: {interface})", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"interface", "interface%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Default interface: {interface} (next fresh launch)"), f, 1));
      assert(irc_display_render(out, sizeof out, "Default interface: {interface} (next fresh launch)", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Default interface: {interface} (next fresh launch)", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"suggestions", "suggestions%n{literal}"}, {"mode", "mode%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Command suggestions: {suggestions} | Composer mode: {mode}"), f, 2));
      assert(irc_display_render(out, sizeof out, "Command suggestions: {suggestions} | Composer mode: {mode}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Command suggestions: {suggestions} | Composer mode: {mode}", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"part", "part%n{literal}"}, {"quit", "quit%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("PART default: {part} | QUIT default: {quit}"), f, 2));
      assert(irc_display_render(out, sizeof out, "PART default: {part} | QUIT default: {quit}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "PART default: {part} | QUIT default: {quit}", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"number", "number%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Effective settings for server {number} (connection edits apply on the next disconnected /connect):"), f, 1));
      assert(irc_display_render(out, sizeof out, "Effective settings for server {number} (connection edits apply on the next disconnected /connect):", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Effective settings for server {number} (connection edits apply on the next disconnected /connect):", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"host", "host%n{literal}"}, {"port", "port%n{literal}"}, {"tls", "tls%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Server: {host} | port: {port} | verified TLS: {tls}"), f, 3));
      assert(irc_display_render(out, sizeof out, "Server: {host} | port: {port} | verified TLS: {tls}", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Server: {host} | port: {port} | verified TLS: {tls}", f, 3) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"nick", "nick%n{literal}"}, {"user", "user%n{literal}"}, {"real", "real%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Nickname: {nick} | username: {user} | real name: {real}"), f, 3));
      assert(irc_display_render(out, sizeof out, "Nickname: {nick} | username: {user} | real name: {real}", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Nickname: {nick} | username: {user} | real name: {real}", f, 3) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"state", "state%n{literal}"}, {"channels", "channels%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Auto-connect: {state} | auto-join: {channels}"), f, 2));
      assert(irc_display_render(out, sizeof out, "Auto-connect: {state} | auto-join: {channels}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Auto-connect: {state} | auto-join: {channels}", f, 2) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"state", "state%n{literal}"}, {"count", "count%n{literal}"}, {"delay", "delay%n{literal}"}, {"interval", "interval%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Automatic commands: {state} | {count} entries | delay {delay} seconds | interval {interval} seconds"), f, 4));
      assert(irc_display_render(out, sizeof out, "Automatic commands: {state} | {count} entries | delay {delay} seconds | interval {interval} seconds", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Automatic commands: {state} | {count} entries | delay {delay} seconds | interval {interval} seconds", f, 4) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"source", "source%n{literal}"}, {"watch", "watch%n{literal}"}, {"ignore", "ignore%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("CA: {source} | watch strings: {watch} | ignore masks: {ignore}"), f, 3));
      assert(irc_display_render(out, sizeof out, "CA: {source} | watch strings: {watch} | ignore masks: {ignore}", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "CA: {source} | watch strings: {watch} | ignore masks: {ignore}", f, 3) && !tiny[0]);
    }
    assert(irc_tr("explicit file") && *irc_tr("explicit file"));
    assert(irc_tr("system trust") && *irc_tr("system trust"));
    { const IrcDisplayField f[] = {{"detail", "detail%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Settings unchanged: {detail}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Settings unchanged: {detail}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Settings unchanged: {detail}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"detail", "detail%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Connection could not start: {detail}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Connection could not start: {detail}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Connection could not start: {detail}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"detail", "detail%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Connection settings are invalid: {detail}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Connection settings are invalid: {detail}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Connection settings are invalid: {detail}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"detail", "detail%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Connection settings were not changed: {detail}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Connection settings were not changed: {detail}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Connection settings were not changed: {detail}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"detail", "detail%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Connection could not be created: {detail}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Connection could not be created: {detail}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Connection could not be created: {detail}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"detail", "detail%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Server not added: {detail}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Server not added: {detail}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Server not added: {detail}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"state", "state%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Auto-connect: {state}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Auto-connect: {state}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Auto-connect: {state}", f, 1) && !tiny[0]);
    }
    puts("Settings and credential path fields: PASS");
    return 0;
}
