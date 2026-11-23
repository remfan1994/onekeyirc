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
  const char *english="Channel list complete: {received} received, {matched} matched, {cached} cached{note}.";
  const IrcDisplayField fields[]={ {"received", "24" }, {"matched", "14" }, {"cached", "12" }, {"note", " (%tag{raw}\\x)" } };
  char shown[1024];
  for(size_t i=0;i<10;++i) {
    assert(setenv("VIPIRC_LANG",langs[i],1)==0);
    assert(strcmp(irc_tr("Away status confirmed by the server."),"Away status confirmed by the server.")!=0);
    assert(irc_display_render(shown,sizeof shown,english,fields,4));
    assert(strstr(shown,"%tag{raw}")!=NULL);
    assert(!irc_display_render(shown,8,english,fields,4));
    assert(shown[0]=='\0');
  }
  assert(setenv("VIPIRC_LANG","en",1)==0);
  assert(irc_display_render(shown,sizeof shown,english,fields,4));
  assert(strstr(shown,"%tag{raw}")!=NULL);
  puts("r43 literal-field and fallback tests PASS");
  return 0;
}
