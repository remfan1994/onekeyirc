#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    char out[4096], tiny[2];
    const IrcDisplayField fields[] = {{"channel", "#name%n{literal}"}, {"command", "NOTICE"}, {"source", "host%p"}, {"message", "raw%s{literal}"}, {"credential", "GPG credential"}, {"id", "987654321"}, {"nick", "Nick%s"}, {"filename", "file%n{abc}.txt"}, {"bytes", "123456"}};
    size_t checked=0;
    assert(irc_tr("Joining {channel}.") != NULL);
    { const IrcDisplayField f[] = {fields[0]};
      assert(irc_display_fields_valid(irc_tr("Joining {channel}."), f, 1));
      assert(irc_display_render(out, sizeof out, "Joining {channel}.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Joining {channel}.", f, 1) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("Joining {channel} with a supplied key.") != NULL);
    { const IrcDisplayField f[] = {fields[0]};
      assert(irc_display_fields_valid(irc_tr("Joining {channel} with a supplied key."), f, 1));
      assert(irc_display_render(out, sizeof out, "Joining {channel} with a supplied key.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Joining {channel} with a supplied key.", f, 1) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("{command} from {source}: {message}") != NULL);
    { const IrcDisplayField f[] = {fields[1], fields[2], fields[3]};
      assert(irc_display_fields_valid(irc_tr("{command} from {source}: {message}"), f, 3));
      assert(irc_display_render(out, sizeof out, "{command} from {source}: {message}", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "{command} from {source}: {message}", f, 3) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("server") != NULL);
    ++checked;
    assert(irc_tr("Usage: /list [MASK] [--min-users N] [--max-users N] [--name TEXT] [--topic TEXT] [--sort channel|users|topic] [--asc|--desc] [--limit N], /list show [--sort channel|users|topic] [--asc|--desc] [--limit N], or /list clear") != NULL);
    ++checked;
    assert(irc_tr("User list on.") != NULL);
    ++checked;
    assert(irc_tr("User list off.") != NULL);
    ++checked;
    assert(irc_tr("DCC file offer #{id} from {nick}: {filename} ({bytes} bytes). Open /dcc to review.") != NULL);
    { const IrcDisplayField f[] = {fields[5], fields[6], fields[7], fields[8]};
      assert(irc_display_fields_valid(irc_tr("DCC file offer #{id} from {nick}: {filename} ({bytes} bytes). Open /dcc to review."), f, 4));
      assert(irc_display_render(out, sizeof out, "DCC file offer #{id} from {nick}: {filename} ({bytes} bytes). Open /dcc to review.", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "DCC file offer #{id} from {nick}: {filename} ({bytes} bytes). Open /dcc to review.", f, 4) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("Cannot retain the file offer: out of memory.") != NULL);
    ++checked;
    printf("Additional interface catalog: %zu entries PASS\n", checked);
    return 0;
}
