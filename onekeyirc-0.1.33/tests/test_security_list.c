#define main legacy_app_test_main
#include "test_app.c"
#undef main

/* Feed untrusted server replies through the actual parser and App cache.
 * The transport fixture is deterministic and never contacts a network. */
static App *list_ready(const char *command)
{
    App *a = fresh(0);
    SendPlan plan;
    register_plain(a);
    output_clear(a);
    assert(app_command(a, command, &plan) == 6);
    assert(a->list.active && !a->list.count);
    return a;
}

static void list_reply(App *a, const char *channel, unsigned users,
                       const char *topic)
{
    char line[IRC_BODY_MAX + 3u];
    int n = snprintf(line, sizeof line, ":server 322 tester %s %u :%s\r\n",
                     channel, users, topic);
    assert(n > 0 && (size_t)n < sizeof line);
    feed(a, line);
}

static void topics_are_safe_before_caching(void)
{
    static const char hostile[] =
        "safe \033]52;c;fixture\007 "
        "\xe2\x80\xae" "admin" "\xe2\x80\xac"
        " \00304,02colored\017 \xc0\xaf caf\xc3\xa9 \xf0\x9f\x98\x80";
    static const char expected[] =
        "safe ?]52;c;fixture? ?admin? colored ?? caf\xc3\xa9 \xf0\x9f\x98\x80";
    App *a = list_ready("/list --limit 0");
    list_reply(a, "#untrusted", 17, hostile);
    assert(a->list.received == 1 && a->list.matched == 1 && a->list.count == 1);
    const ChannelListEntry *entry = &a->list.entries[0];
    assert(!strcmp(entry->channel, "#untrusted") && entry->users == 17);
    assert(!strcmp(entry->topic, expected));
    assert(irc_utf8_valid(entry->topic));
    assert(!strchr(entry->topic, '\033') && !strchr(entry->topic, '\007'));
    assert(!strstr(entry->topic, "\xe2\x80\xae"));
    assert(!strstr(entry->topic, "\xe2\x80\xac"));
    destroy(a);
}

static void all_filtered_unicode_controls(void)
{
    static const char *const controls[] = {
        "\xc2\x80", "\xc2\x9b", "\xd8\x9c", "\xe2\x80\x8e",
        "\xe2\x80\x8f", "\xe2\x80\xaa", "\xe2\x80\xab",
        "\xe2\x80\xac", "\xe2\x80\xad", "\xe2\x80\xae",
        "\xe2\x81\xa6", "\xe2\x81\xa7", "\xe2\x81\xa8",
        "\xe2\x81\xa9", "\xef\xbb\xbf"
    };
    App *a = list_ready("/list --limit 0");
    for (size_t i = 0; i < sizeof controls / sizeof controls[0]; ++i) {
        char topic[32];
        snprintf(topic, sizeof topic, "left%sright", controls[i]);
        list_reply(a, "#controls", 1, topic);
        assert(!strcmp(a->list.entries[i].topic, "left?right"));
    }
    list_reply(a, "#separators", 2, "left\xe2\x80\xa8mid\xe2\x80\xa9right");
    assert(!strcmp(a->list.entries[a->list.count - 1u].topic, "left mid right"));
    destroy(a);
}

static void filters_and_sort_use_visible_topics(void)
{
    App *a = list_ready("/list --topic security --sort topic --asc --limit 0");
    list_reply(a, "#styled-zulu", 9, "\00304Zulu se\002cur\017ity");
    list_reply(a, "#plain-alpha", 5, "Alpha security");
    list_reply(a, "#different", 7, "Other subject");
    assert(a->list.received == 3 && a->list.matched == 2 && a->list.count == 2);
    feed(a, ":server 323 tester :End of LIST\r\n");
    assert(a->list.complete && !a->list.active);
    assert(!strcmp(a->list.entries[0].channel, "#plain-alpha"));
    assert(!strcmp(a->list.entries[0].topic, "Alpha security"));
    assert(!strcmp(a->list.entries[1].channel, "#styled-zulu"));
    assert(!strcmp(a->list.entries[1].topic, "Zulu security"));
    app_list_sort(a, APP_LIST_SORT_TOPIC, 1);
    assert(!strcmp(a->list.entries[0].channel, "#styled-zulu"));
    destroy(a);

    a = list_ready("/list --topic ?admin? --limit 0");
    list_reply(a, "#bidi", 1, "\xe2\x80\xae" "admin" "\xe2\x80\xac");
    assert(a->list.received == 1 && a->list.matched == 1 && a->list.count == 1);
    assert(!strcmp(a->list.entries[0].topic, "?admin?"));
    destroy(a);
}

static void long_valid_topic_and_empty_topic(void)
{
    char topic[460];
    memset(topic, 'a', 450u);
    strcpy(topic + 450u, "\xf0\x9f\x98\x80");
    App *a = list_ready("/list --limit 0");
    list_reply(a, "#long", 3, topic);
    list_reply(a, "#empty", 0, "");
    assert(a->list.count == 2 && a->list.matched == 2);
    assert(!strcmp(a->list.entries[0].topic, topic));
    assert(irc_utf8_valid(a->list.entries[0].topic));
    assert(!*a->list.entries[1].topic && !a->list.entries[1].users);
    destroy(a);
}

int main(void)
{
    topics_are_safe_before_caching();
    all_filtered_unicode_controls();
    filters_and_sort_use_visible_topics();
    long_valid_topic_and_empty_topic();
    puts("LIST security: unsafe controls, invalid UTF-8, styling, safe Unicode, empty/long topics, and visible-topic filtering/sorting passed");
    return 0;
}
