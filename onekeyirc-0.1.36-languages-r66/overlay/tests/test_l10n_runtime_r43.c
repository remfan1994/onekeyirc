/* r43: named-field templates never interpret untrusted values as formats. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  static const char *langs[]={"id","ms","tr","es","pt","fr","de","ru","it","ar"};
  const char *english="{item} is selected in the watch log.";
  const IrcDisplayField fields[]={ {"item", "#nickname%{server}\\raw" } };
  char shown[1024];
  for(size_t i=0;i<10;++i) {
    assert(setenv("ONEKEYIRC_LANG",langs[i],1)==0);
    assert(strcmp(irc_tr("Command response reader"),"Command response reader")!=0);
    assert(irc_display_render(shown,sizeof shown,english,fields,1));
    assert(strstr(shown,"#nickname%{server}")!=NULL);
    assert(!irc_display_render(shown,8,english,fields,1));
    assert(shown[0]=='\0');
  }
  assert(setenv("ONEKEYIRC_LANG","en",1)==0);
  assert(irc_display_render(shown,sizeof shown,english,fields,1));
  assert(strstr(shown,"#nickname%{server}")!=NULL);
  puts("r43 literal-field and fallback tests PASS");
  return 0;
}
