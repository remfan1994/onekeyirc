#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
 const char *keys[]={
 "GPG is encrypting with the selected local key. Settings have not been applied.",
 "Application preferences apply to every server. Server panels edit one connection profile.",
 "Enter the IRC server hostname or IP address, without a URL or port. For ZNC or soju setup use /panel.",
 "Enter the IRC server or bouncer hostname or IP address, without a URL or port. Bouncers connect at their provider's address.",
 "Your nickname on IRC.",
 "A public description; it need not be your legal name. '-' clears it.",
 "Connect this profile automatically on future starts: yes or no.",
 "Comma-separated channels, for example #chat,#friends. '-' clears saved joins.",
 "none = no automatic account login; sasl = log in during connection. Verified TLS is required.",
 "none, file (private plaintext file), gpg (create or use an encrypted file), or plain (hidden entry saved in config).",
 "Stored directly in the private configuration file. Password input is hidden.",
 "Password to encrypt. Input is hidden; no plaintext password file is created.",
 "Type the same password again to confirm it.",
 "Optional CA file. Empty uses the system trust store; '-' clears. Server identity is always verified.",
 "50 to 5000 retained lines. Native output also has hard record and byte bounds.",
 "direct = one Enter to send; review = review the log and confirm with another Enter. Applies to all servers.",
 "line = one input row below the live tail; fullscreen = a multi-line editor with a smaller live-tail pane. Applies to vipirc -i and onekeyirc across all servers.",
 "compact = time, sender, text; full = also date, server, channel. Applies to all servers.",
 "Show routine message-queue and pacing notifications. Errors and multi-message warnings remain visible. Applies to all servers.",
 "Show low-priority local feedback such as draft cancellation, input-cleared, and preserved-draft notices. Errors and confirmations stay visible. Applies to all servers.",
 "When yes, only one background job may exist in this client namespace, making bare --resume unambiguous. Multiple-server connections inside that session are unaffected.",
 "common = useful commands first; history = recent accepted commands; frequent = your most-used commands; alphabetical = names in order; contextual = commands for the screen that opened command entry. Down selects a suggestion; Enter inserts it. Applies to all servers.",
 "yes = terminal Ctrl-C interrupts and Ctrl-Z suspends; no (default) = cancel an unsent compose instead. Applies now in this terminal; Save retains it for later launches. External kill signals still work.",
 "Next fresh launch: ncurses is vipirc's full interface; idle is minimalist. Explicit -i or -N overrides the saved choice for that launch. onekeyirc always uses idle. The current interface stays open.",
 "Match this text as a mention.",
 "A nickname or nick!user@host mask.",
 "A blank new entry ends the list; '-' removes the shown entry."
 };
 const char *langs[]={"id","ms","tr","es","pt","fr","de","ru","it","ar"};
 for(size_t i=0;i<10;i++){
  assert(setenv("VIPIRC_LANG",langs[i],1)==0);
  assert(setenv("ONEKEYIRC_LANG",langs[i],1)==0);
  for(size_t j=0;j<sizeof keys/sizeof *keys;j++){
   const char *x=irc_tr(keys[j]); assert(x && strcmp(x,keys[j])!=0 && strlen(x)<=256);
  }
  const char *untrusted="%p{nick}/home/test";
  assert(strcmp(irc_tr(untrusted),untrusted)==0);
 }
 assert(setenv("VIPIRC_LANG","en",1)==0);
 assert(setenv("ONEKEYIRC_LANG","en",1)==0);
 for(size_t j=0;j<sizeof keys/sizeof *keys;j++)assert(strcmp(irc_tr(keys[j]),keys[j])==0);
 puts("PASS: r51 27 fixed setup help strings x ten locales and English fallback");
}
