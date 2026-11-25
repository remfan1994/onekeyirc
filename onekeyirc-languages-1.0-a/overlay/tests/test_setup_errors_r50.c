/* Standalone compiled catalog test; does not access credentials or the network. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
 const char *keys[] = {
  "Choose one of the displayed panel numbers.",
  "That profile could not be loaded; correct its configuration file first.",
  "No configuration is available.",
  "Cannot allocate the configuration draft; nothing changed.",
  "Use /panel to edit this bouncer profile, including its network and password source.",
  "Choose 0 for application preferences or a displayed server panel number.",
  "Server and nickname are required; Escape returns to the questions.",
  "Enter an IRC server hostname or IP address.",
  "A nickname is required.",
  "An IRC username is required.",
  "Enter yes or no for automatic startup connection.",
  "Enter none or sasl.",
  "History lines must be a number from 50 to 5000.",
  "Enter direct (one Enter) or review (two Enters).",
  "Enter line or fullscreen.",
  "Enter compact or full.",
  "Enter yes or no for routine queue notifications.",
  "Enter yes or no for program messages.",
  "Enter yes or no for persistent logging.",
  "Enter yes or no for single-session mode.",
  "Choose common, history, frequent, alphabetical, or contextual.",
  "Enter yes or no for Use signals.",
  "Enter idle or ncurses for the next launch interface.",
  "Invalid configuration question; cancel and start again.",
  "Server, nickname, and IRC username are required; Escape returns to the questions.",
  "GPG file creation is disabled in --test-mode; no helper, keyring, or file was accessed.",
  "Cannot time the GPG operation; nothing changed.",
  "Creating encrypted credentials; settings have not been applied.",
  " Completed encrypted files are retained for this setup; retry Finish or cancel.",
  "The selected server profile changed; encryption was cancelled without applying settings.",
  "The application configuration changed; cancel and reopen the panel.",
  "The selected server profile changed; cancel and reopen the panel.",
  "Choose 1 (save) or 2 (apply without saving).",
  "Choose 1 (save and connect), 2 (save only), 3 (connect once), or 4 (apply only)."
 };
 const char *langs[] = {"id","ms","tr","es","pt","fr","de","ru","it","ar"};
 for (size_t i=0;i<10;++i) {
  assert(setenv("VIPIRC_LANG",langs[i],1)==0);
  assert(setenv("ONEKEYIRC_LANG",langs[i],1)==0);
  for(size_t j=0;j<sizeof keys/sizeof keys[0];++j) {
   const char *x=irc_tr(keys[j]);assert(x && strcmp(x,keys[j])!=0 && strlen(x)<=256);
  }
  const char *untrusted="nick%p{error}../../server.txt";
  assert(strcmp(irc_tr(untrusted),untrusted)==0);
 }
 assert(setenv("VIPIRC_LANG","en",1)==0);
 assert(setenv("ONEKEYIRC_LANG","en",1)==0);
 for(size_t j=0;j<sizeof keys/sizeof keys[0];++j) assert(!strcmp(irc_tr(keys[j]),keys[j]));
 puts("r50: 34 setup display messages / ten locales and fallback PASS");
 return 0;
}
