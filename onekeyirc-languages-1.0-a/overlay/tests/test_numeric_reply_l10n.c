/* Source-linked 21-key WHOIS/numeric display test. No sockets or GPG. */
#define IRC_L10N_IMPLEMENTATION
#include "../src/l10n.h"
#include "../src/display_l10n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const keys[] = {
    "Unsupported case mapping {mapping}; using RFC1459.",
    "Your user modes: {modes}",
    "{nick} is away: {message}",
    "{nick} is {user}@{host} ({realname})",
    "{nick} was {user}@{host} ({realname})",
    "Server for {nick}: {server} ({info})",
    "{nick} has been idle for {seconds} seconds; connected since {time}.",
    "{nick} has been idle for {seconds} seconds.",
    "End of user information for {nick}: {message}",
    "{nick}'s channels: {channels}",
    "Network channel list:",
    "{channel} ({users} users): {topic}",
    "End of channel list: {message}",
    "Modes for {channel}: {modes}",
    "{nick} is signed in as {account}.",
    "No topic is set for {channel}: {message}",
    "Topic for {channel}: {topic}",
    "Topic for {channel} was set by {setter} on {time}.",
    "No message of the day: {message}",
    "Server: {message}",
    "Reply received without a message.",
};
static const IrcDisplayField samples[] = {
  {"mapping", "nonstandard%n"}, {"modes", "+iw%"},
  {"nick", "Alice%p"}, {"message", "Raw {untrusted} 100%n"},
  {"user", "u%"}, {"host", "host.example"},
  {"realname", "Name %n {literal}"}, {"server", "irc.example"},
  {"info", "realm {tag}"}, {"seconds", "18446744073709551615"},
  {"time", "1970-01-01 00:00:00 UTC"},
  {"channels", "#a%p #b"}, {"channel", "#c{literal}"},
  {"users", "900"}, {"topic", "Topic {not_a_field} 100%"},
  {"account", "acct%n"}, {"setter", "Bob%s"}
};
int main(void)
{
  size_t count=0;
  for (size_t k=0;k<sizeof keys/sizeof keys[0];++k) {
    const char *source=keys[k];
    if (!strchr(source,'{')) {
      if (!irc_tr(source) || !*irc_tr(source)) return 1;
      count++; continue;
    }
    IrcDisplayField f[4]; size_t n=0;
    for (const char *p=source;*p;++p) {
      if (*p!='{') continue;
      const char *e=strchr(p+1,'}');
      if (!e) return 2;
      size_t j;
      for(j=0;j<sizeof samples/sizeof samples[0];++j)
        if (strlen(samples[j].name)==(size_t)(e-p-1) &&
            memcmp(samples[j].name,p+1,(size_t)(e-p-1))==0) break;
      if(j==sizeof samples/sizeof samples[0]||n>=4) return 3;
      f[n++]=samples[j]; p=e;
    }
    char result[2048];
    if (!irc_display_render(result,sizeof result,source,f,n)) {
      fprintf(stderr,"render failed: %s\n",source); return 4;
    }
    for(size_t i=0;i<n;++i)
      if (!strstr(result,f[i].value)) {
        fprintf(stderr,"literal field lost: %s\n",source); return 5;
      }
    char tiny[3]={'x','y','z'};
    if (irc_display_render(tiny,sizeof tiny,source,f,n)||tiny[0]) return 6;
    count++;
  }
  printf("numeric reply display: %zu catalog keys, bounded fields PASS\n",count);
  return 0;
}
