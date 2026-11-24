/* Display-only transfer notices. Split the offer into two complete sentences
 * to stay within the named-field renderer's four-field limit. */
#ifndef IRC_TRANSFER_NOTICE_L10N_H
#define IRC_TRANSFER_NOTICE_L10N_H
#include "l10n.h"
#include "display_l10n.h"
#include <stdio.h>
static int irc_transfer_offer_notice(char *out, size_t cap, unsigned long long id,
        const char *nick, const char *filename, unsigned size,
        const char *address, unsigned port)
{
    char number[32], amount[32], port_text[32], offer[1024], peer[512];
    if (!out || !cap) return 0;
    out[0] = 0;
    (void)snprintf(number, sizeof number, "%llu", id);
    (void)snprintf(amount, sizeof amount, "%u", size);
    (void)snprintf(port_text, sizeof port_text, "%u", port);
    const IrcDisplayField offer_fields[] = {{"id", number}, {"nick", nick},
        {"filename", filename}, {"size", amount}};
    const IrcDisplayField peer_fields[] = {{"address", address}, {"port", port_text}};
    if (!irc_display_render(offer, sizeof offer,
            "File offer #{id} from {nick}: {filename} ({size} bytes).", offer_fields, 4) ||
        !irc_display_render(peer, sizeof peer,
            "Peer {address}:{port}. /dcc list shows details; acceptance is explicit.", peer_fields, 2)) return 0;
    int length = snprintf(out, cap, "%s %s", offer, peer);
    if (length < 0 || (size_t)length >= cap) { out[0] = 0; return 0; }
    return 1;
}
static int irc_transfer_result_notice(char *out, size_t cap, unsigned long long id,
        const char *state, const char *filename, const char *error)
{
    char number[32];
    (void)snprintf(number, sizeof number, "%llu", id);
    const IrcDisplayField fields[] = {{"id", number}, {"state", irc_tr(state)},
        {"filename", filename}, {"error", error}};
    return irc_display_render(out, cap, *error ?
        "File transfer #{id} ({state}): {filename}: {error}" :
        "File transfer #{id} ({state}): {filename}", fields, *error ? 4 : 3);
}
#endif
