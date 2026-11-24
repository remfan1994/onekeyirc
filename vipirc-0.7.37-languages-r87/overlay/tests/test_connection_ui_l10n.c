#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    char out[4096], tiny[2];
    { const IrcDisplayField f[] = {{"value", "value%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("mask {value}"), f, 1));
      assert(irc_display_render(out, sizeof out, "mask {value}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "mask {value}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"value", "value%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("min {value}"), f, 1));
      assert(irc_display_render(out, sizeof out, "min {value}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "min {value}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"value", "value%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("max {value}"), f, 1));
      assert(irc_display_render(out, sizeof out, "max {value}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "max {value}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"value", "value%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("name {value}"), f, 1));
      assert(irc_display_render(out, sizeof out, "name {value}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "name {value}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"value", "value%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("topic {value}"), f, 1));
      assert(irc_display_render(out, sizeof out, "topic {value}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "topic {value}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"value", "value%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("limit {value}"), f, 1));
      assert(irc_display_render(out, sizeof out, "limit {value}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "limit {value}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"view", "view%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Return to: {view}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Return to: {view}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Return to: {view}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"status", "status%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Connection: {status}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Connection: {status}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Connection: {status}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"nick", "nick%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Nick: {nick}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Nick: {nick}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Nick: {nick}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Users: {count}"), f, 1));
      assert(irc_display_render(out, sizeof out, "Users: {count}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Users: {count}", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Users: {count}+ (limit)"), f, 1));
      assert(irc_display_render(out, sizeof out, "Users: {count}+ (limit)", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Users: {count}+ (limit)", f, 1) && !tiny[0]);
    }
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("{count} users"), f, 1));
      assert(irc_display_render(out, sizeof out, "{count} users", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "{count} users", f, 1) && !tiny[0]);
    }
    puts("Connection and directory localization: PASS");
    return 0;
}
