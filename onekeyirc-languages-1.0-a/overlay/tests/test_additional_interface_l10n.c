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
    assert(irc_tr("That channel view is closed. Join it again or clear mentions.") != NULL);
    ++checked;
    assert(irc_tr("That source conversation is no longer available.") != NULL);
    ++checked;
    assert(irc_tr("Channel directory is still receiving results.") != NULL);
    ++checked;
    assert(irc_tr("Showing the cached channel directory.") != NULL);
    ++checked;
    assert(irc_tr("No completed channel directory is cached; use /list first.") != NULL);
    ++checked;
    assert(irc_tr("Decrypting {credential} with GPG.") != NULL);
    { const IrcDisplayField f[] = {fields[4]};
      assert(irc_display_fields_valid(irc_tr("Decrypting {credential} with GPG."), f, 1));
      assert(irc_display_render(out, sizeof out, "Decrypting {credential} with GPG.", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Decrypting {credential} with GPG.", f, 1) && !tiny[0]);
    }
    ++checked;
    assert(irc_tr("server password") != NULL);
    ++checked;
    assert(irc_tr("SASL password") != NULL);
    ++checked;
    assert(irc_tr("Credential request is not permitted in this connection state.") != NULL);
    ++checked;
    printf("Additional interface catalog: %zu entries PASS\n", checked);
    return 0;
}
