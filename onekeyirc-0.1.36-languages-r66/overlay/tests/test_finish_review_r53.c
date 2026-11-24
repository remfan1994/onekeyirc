#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void){
 static const char *const keys[]={
    "1 Save | 2 Apply without saving",
    "History lines:",
    "Send mode:",
    "Composer mode:",
    "Output format:",
    "Queue notices:",
    "Program messages:",
    "Persistent logging:",
    "Single-session mode:",
    "Use signals:",
    "Command suggestions:",
    "Default interface:",
    "Configuration:",
    "idle (fixed in onekeyirc)",
    "idle (next fresh launch)",
    "ncurses (next fresh launch)",
    "1 Save and connect | 2 Save only | 3 Connect once | 4 Apply without saving",
    "Server:",
    "Nickname:",
    "Connection changes apply on the next connection.",
    "new saved profile",
    "Previous server's startup/authentication settings were cleared in this draft.",
    "Previous authentication settings and startup commands in this draft were cleared; supply the current account's password.",
    "This server is already active; choose Save only or Apply without saving.",
    "verified TLS",
    "unencrypted",
    "Bouncer:",
    "Login identity:",
    "Channels: restored by the bouncer",
    "Automatic away:",
    "(incomplete)",
    "client rules enabled",
    "managed by bouncer",
    "Authentication",
    "encrypted file",
    "created",
    "not created; created at Finish",
    "Local key:",
    "Encrypted credential created.",
    "Encrypted credentials created.",
    "Use /autoauth status on this server for full file paths.",
    "Full file paths are in the Finish review; settings may not be applied.",
    "Server file:",
    "Authentication file:",
 };
 const char *const langs[]={"id","ms","tr","es","pt","fr","de","ru","it","ar"};
 for(size_t l=0;l<10;l++){
  assert(setenv("VIPIRC_LANG",langs[l],1)==0);
  assert(setenv("ONEKEYIRC_LANG",langs[l],1)==0);
  for(size_t i=0;i<sizeof(keys)/sizeof(*keys);i++) {
    const char *v=irc_tr(keys[i]);assert(v && strlen(v)>0 && strlen(v)<=256);
  }
  const char *s=irc_tr("Encrypted credentials created.");
  assert(s && strlen(s)>0);
  assert(strstr(irc_tr("Use /autoauth status on this server for full file paths."),"/autoauth status"));
 }
 assert(setenv("VIPIRC_LANG","en",1)==0);
 assert(setenv("ONEKEYIRC_LANG","en",1)==0);
 for(size_t i=0;i<sizeof(keys)/sizeof(*keys);i++)assert(strcmp(irc_tr(keys[i]),keys[i])==0);
 puts("PASS: Finish review catalogs and English fallback in ten locales");
 return 0;
}
