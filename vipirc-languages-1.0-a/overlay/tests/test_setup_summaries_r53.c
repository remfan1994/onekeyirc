#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include "display_l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void){
 static const char *const keys[]={
    "Your bouncer account, not the upstream NickServ account.",
    "The network name already configured on your bouncer. The bouncer restores its joined channels.",
    "Optional stable name for this device, such as laptop. '-' clears it.",
    "Up to 63 ASCII letters, digits, '.', '_' or '-'; do not include '/' or '@'. Changing identity clears this draft's password source and startup commands.",
    "Enter accepts this offered absolute path and prepares its private directory (0700). You may enter a different path in an existing private directory.",
    "Enter a new file in a directory you own with mode 0700.",
    "The destination is checked before the key and password questions. The encrypted file is created only at Finish; existing files are never replaced. Paths are literal, with no shell expansion.",
    "GPG-encrypted",
    "Private plaintext",
    "path; no shell expansion. The file is read only when connecting.",
    " (unavailable)",
    "Proposed (not created)",
    "Configured",
    "credential file:",
    "Application preferences saved.",
    "Application preferences applied without saving.",
    "Server {number}: settings saved.",
    "Server {number}: settings applied without saving.",
    "Server {number}: settings saved; connecting.",
    "Server {number}: settings applied without saving; connecting.",
    "Settings saved, but connection failed: {error}",
    "Settings applied, but connection failed: {error}",
    "Encrypted files were kept because the saved configuration may reference them. {error}",
    "Bouncer password file",
    "Server password file",
    "Authentication password file",
 };
 const char *const langs[]={"id","ms","tr","es","pt","fr","de","ru","it","ar"};
 for(size_t l=0;l<sizeof langs/sizeof *langs;l++){
  assert(setenv("VIPIRC_LANG",langs[l],1)==0);
  assert(setenv("ONEKEYIRC_LANG",langs[l],1)==0);
  for(size_t i=0;i<sizeof keys/sizeof *keys;i++){
   const char *v=irc_tr(keys[i]);assert(v && strcmp(v,keys[i])!=0 && strlen(v)<=256);
  }
  char b[2048];
  IrcDisplayField f[]={{"error","The server replied: %n {nick} /tmp/path"}};
  assert(irc_display_render(b,sizeof b,"Settings saved, but connection failed: {error}",f,1));
  assert(strstr(b,"%n {nick} /tmp/path")!=NULL);
  assert(!irc_display_render(b,12,"Settings saved, but connection failed: {error}",f,1));
  assert(b[0]=='\0');
  IrcDisplayField n[]={{"number","42"}};
  assert(irc_display_render(b,sizeof b,"Server {number}: settings saved; connecting.",n,1));
  assert(strstr(b,"42")!=NULL);
 }
 assert(setenv("VIPIRC_LANG","en",1)==0);
 assert(setenv("ONEKEYIRC_LANG","en",1)==0);
 for(size_t i=0;i<sizeof keys/sizeof *keys;i++)assert(strcmp(irc_tr(keys[i]),keys[i])==0);
 puts("PASS: r53 26 setup phrases in ten locales and literal dynamic values");
 return 0;
}
