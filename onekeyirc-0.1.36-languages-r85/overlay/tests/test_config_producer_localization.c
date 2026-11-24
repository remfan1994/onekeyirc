/* Verify real config_validate() -> display translations without mutating
 * validation/security logic or interpreting variable text as a format. */
#define _POSIX_C_SOURCE 200809L
#define IRC_L10N_IMPLEMENTATION
#include "config.h"
#include "l10n.h"
#include "security_diagnostic_l10n.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void fail(const char *step, const char *got, const char *want) {
  fprintf(stderr,"%s: got '%s', expected '%s'\n", step, got, want);
  exit(1);
}
#define CASE(member, value, expected) do { \
  config_defaults(&c); c.member = (value); \
  if (config_validate(&c,err,sizeof(err)) != -1 || strcmp(err,expected)!=0) \
    fail(#member,err,expected); \
  const char *translated=irc_security_diagnostic(err,display,sizeof(display)); \
  if (expect_translation ? strcmp(translated,err)==0 : strcmp(translated,err)!=0) \
    fail("translation mismatch",translated,err); \
  if (strstr(translated,"%n")) fail("unsafe format",translated,"no directives"); \
  count++; \
} while(0)

int main(void) {
  Config c;
  char err[512],display[2048];
  size_t count=0;
  int expect_translation=getenv("TEST_EXPECT_TRANSLATION") && *getenv("TEST_EXPECT_TRANSLATION");
  CASE(autoconnect,2,"autoconnect must be true or false");
  CASE(bouncer_type,99,"bouncer.type must be none, znc or soju");
  CASE(bouncer_away,2,"bouncer.automatic_away must be true or false");
  CASE(send_review,2,"send_mode must be direct or review");
  CASE(use_signals,2,"use_signals must be true or false");
  CASE(output_full,2,"output format must be compact or full");
  CASE(queue_notices,2,"queue_notices must be true or false");
  CASE(program_messages,2,"program_messages must be true or false");
  CASE(persistent_logging,2,"persistent_logging must be true or false");
  CASE(single_session,2,"single_session must be true or false");
  CASE(background_away,2,"background_away must be true or false");
  CASE(autoaway_minutes,1500,"autoaway_minutes must be between 0 and 1440");
  CASE(default_interface,99,"default_interface must be ncurses or idle");
  CASE(command_suggestions,99,"command_suggestions must be alphabetical, common, history, frequent or contextual");
  CASE(composer_fullscreen,2,"composer_mode must be line or fullscreen");
  CASE(autocommands.enabled,2,"autocommands.enabled must be true or false");
  CASE(autocommands.delay_seconds,301,"autocommands.delay_seconds must be between 0 and 300");
  CASE(autocommands.interval_seconds,1,"autocommands.interval_seconds must be between 2 and 60");
  CASE(minimal,99,"Internal configuration format flag is invalid");
  CASE(user_list,2,"user_list must be true or false");
  CASE(mouse,2,"mouse must be true or false");
  CASE(hints,2,"hints must be true or false");
  CASE(shortcut_labels,2,"shortcut_labels must be true or false");
  CASE(focus_style,99,"focus_style must be reverse or brackets");
  CASE(command_usage_count,CONFIG_COMMAND_USAGE_MAX+1,"Too many retained command usage counters");
  config_defaults(&c);
  strcpy(c.username,"!invalid");
  if (config_validate(&c,err,sizeof(err))!=-1 || strcmp(err,"username must be an ASCII IRC token of at most 32 bytes")!=0)
    fail("username",err,"username must be an ASCII IRC token of at most 32 bytes");
  if (expect_translation == (strcmp(irc_security_diagnostic(err,display,sizeof display),err)==0))
    fail("username translation",err,"translated/fallback");
  ++count;
  config_defaults(&c);
  c.auth_user[0]=(char)0xff; c.auth_user[1]=0;
  if (config_validate(&c,err,sizeof(err))!=-1 || strcmp(err,"auth_user must be valid UTF-8")!=0)
    fail("auth_user",err,"auth_user must be valid UTF-8");
  if (expect_translation == (strcmp(irc_security_diagnostic(err,display,sizeof display),err)==0))
    fail("auth_user translation",err,"translated/fallback");
  ++count;
  config_defaults(&c);
  c.background_away=1;c.background_away_message[0]=0;
  if (config_validate(&c,err,sizeof(err))!=-1 || strcmp(err,"background_away_message cannot be empty while background_away is enabled")!=0)
    fail("background_away_message",err,"background_away_message cannot be empty while background_away is enabled");
  if (expect_translation == (strcmp(irc_security_diagnostic(err,display,sizeof display),err)==0))
    fail("background_away_message translation",err,"translated/fallback");
  ++count;
  config_defaults(&c);
  c.autoaway_minutes=5;c.autoaway_message[0]=0;
  if (config_validate(&c,err,sizeof(err))!=-1 || strcmp(err,"autoaway_message cannot be empty while autoaway is enabled")!=0)
    fail("autoaway_message",err,"autoaway_message cannot be empty while autoaway is enabled");
  if (expect_translation == (strcmp(irc_security_diagnostic(err,display,sizeof display),err)==0))
    fail("autoaway_message translation",err,"translated/fallback");
  ++count;
  {
    const char raw[]="Raw filename /tmp/a%nb\303\251";
    if (irc_security_diagnostic(raw,display,sizeof display)!=raw)
      fail("literal unknown",display,raw);
  }
  printf("config producer/display: %zu conditions PASS\n",count);
  return 0;
}
