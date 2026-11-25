#define IRC_L10N_IMPLEMENTATION
#include "../src/transfer_notice_l10n.h"
#include <assert.h>
#include <stdlib.h>
int main(void) {
    char out[4096], tiny[2];
    assert(irc_transfer_offer_notice(out, sizeof out, 42, "Nick%n", "file{a}.txt", 123, "192.0.2.5", 4567));
    assert(strstr(out, "42") && strstr(out, "Nick%n") && strstr(out, "file{a}.txt") && strstr(out, "123") && strstr(out, "192.0.2.5") && strstr(out, "4567") && strstr(out, "/dcc list"));
    assert(!irc_transfer_offer_notice(tiny, sizeof tiny, 42, "Nick%n", "file{a}.txt", 123, "192.0.2.5", 4567) && !tiny[0]);
    const char *states[] = {"complete", "rejected", "cancelled", "failed"};
    for (size_t j=0; j<4; ++j) {
        assert(irc_transfer_result_notice(out, sizeof out, 42, states[j], "file{a}.txt", "error%n{raw}"));
        assert(strstr(out, "42") && strstr(out, irc_tr(states[j])) && strstr(out, "file{a}.txt") && strstr(out, "error%n{raw}"));
        assert(irc_transfer_result_notice(out, sizeof out, 42, states[j], "file{a}.txt", ""));
        assert(strstr(out, "42") && strstr(out, irc_tr(states[j])) && strstr(out, "file{a}.txt") && !strstr(out, "error%n{raw}"));
        assert(!irc_transfer_result_notice(tiny, sizeof tiny, 42, states[j], "file{a}.txt", "") && !tiny[0]);
    }
    assert(irc_tr("Network channel list requested; directory updates as replies arrive.") && *irc_tr("Network channel list requested; directory updates as replies arrive."));
    assert(irc_tr("Network channel list requested with a server mask; directory updates as replies arrive.") && *irc_tr("Network channel list requested with a server mask; directory updates as replies arrive."));
    assert(irc_tr("File transfer #{id} ({state}): {filename}") && *irc_tr("File transfer #{id} ({state}): {filename}"));
    { const IrcDisplayField f[] = {{"id", "id%n{literal}"}, {"state", "state%n{literal}"}, {"filename", "filename%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("File transfer #{id} ({state}): {filename}"), f, 3));
      assert(irc_display_render(out, sizeof out, "File transfer #{id} ({state}): {filename}", f, 3));
      for (size_t j=0; j<3; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "File transfer #{id} ({state}): {filename}", f, 3) && !tiny[0]);
    }
    assert(irc_tr("File transfer #{id} ({state}): {filename}: {error}") && *irc_tr("File transfer #{id} ({state}): {filename}: {error}"));
    { const IrcDisplayField f[] = {{"id", "id%n{literal}"}, {"state", "state%n{literal}"}, {"filename", "filename%n{literal}"}, {"error", "error%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("File transfer #{id} ({state}): {filename}: {error}"), f, 4));
      assert(irc_display_render(out, sizeof out, "File transfer #{id} ({state}): {filename}: {error}", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "File transfer #{id} ({state}): {filename}: {error}", f, 4) && !tiny[0]);
    }
    assert(irc_tr("File offer #{id} from {nick}: {filename} ({size} bytes).") && *irc_tr("File offer #{id} from {nick}: {filename} ({size} bytes)."));
    { const IrcDisplayField f[] = {{"id", "id%n{literal}"}, {"nick", "nick%n{literal}"}, {"filename", "filename%n{literal}"}, {"size", "size%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("File offer #{id} from {nick}: {filename} ({size} bytes)."), f, 4));
      assert(irc_display_render(out, sizeof out, "File offer #{id} from {nick}: {filename} ({size} bytes).", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "File offer #{id} from {nick}: {filename} ({size} bytes).", f, 4) && !tiny[0]);
    }
    assert(irc_tr("Peer {address}:{port}. /dcc list shows details; acceptance is explicit.") && *irc_tr("Peer {address}:{port}. /dcc list shows details; acceptance is explicit."));
    { const IrcDisplayField f[] = {{"address", "address%n{literal}"}, {"port", "port%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Peer {address}:{port}. /dcc list shows details; acceptance is explicit."), f, 2));
      assert(irc_display_render(out, sizeof out, "Peer {address}:{port}. /dcc list shows details; acceptance is explicit.", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Peer {address}:{port}. /dcc list shows details; acceptance is explicit.", f, 2) && !tiny[0]);
    }
    assert(irc_tr("Network channels") && *irc_tr("Network channels"));
    assert(irc_tr("Users") && *irc_tr("Users"));
    assert(irc_tr("Topic") && *irc_tr("Topic"));
    assert(irc_tr("cached") && *irc_tr("cached"));
    assert(irc_tr("cache truncated") && *irc_tr("cache truncated"));
    assert(irc_tr("Sort: {column} {direction} | received {received} | matched {matched}") && *irc_tr("Sort: {column} {direction} | received {received} | matched {matched}"));
    { const IrcDisplayField f[] = {{"column", "column%n{literal}"}, {"direction", "direction%n{literal}"}, {"received", "received%n{literal}"}, {"matched", "matched%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("Sort: {column} {direction} | received {received} | matched {matched}"), f, 4));
      assert(irc_display_render(out, sizeof out, "Sort: {column} {direction} | received {received} | matched {matched}", f, 4));
      for (size_t j=0; j<4; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "Sort: {column} {direction} | received {received} | matched {matched}", f, 4) && !tiny[0]);
    }
    assert(irc_tr("showing {count}") && *irc_tr("showing {count}"));
    { const IrcDisplayField f[] = {{"count", "count%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("showing {count}"), f, 1));
      assert(irc_display_render(out, sizeof out, "showing {count}", f, 1));
      for (size_t j=0; j<1; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "showing {count}", f, 1) && !tiny[0]);
    }
    assert(irc_tr("ascending") && *irc_tr("ascending"));
    assert(irc_tr("descending") && *irc_tr("descending"));
    assert(irc_tr("(no server)") && *irc_tr("(no server)"));
    assert(irc_tr("File offer from {nick} rejected: {error}") && *irc_tr("File offer from {nick} rejected: {error}"));
    { const IrcDisplayField f[] = {{"nick", "nick%n{literal}"}, {"error", "error%n{literal}"}};
      assert(irc_display_fields_valid(irc_tr("File offer from {nick} rejected: {error}"), f, 2));
      assert(irc_display_render(out, sizeof out, "File offer from {nick} rejected: {error}", f, 2));
      for (size_t j=0; j<2; ++j) assert(strstr(out, f[j].value));
      assert(!irc_display_render(tiny, sizeof tiny, "File offer from {nick} rejected: {error}", f, 2) && !tiny[0]);
    }
    puts("Transfer notices and channel directory: PASS");
    return 0;
}
