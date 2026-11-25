/* Actual Settings/editor/rendering code; only its application callbacks are
 * small fixtures. File saving/loading still uses the production private writer. */
#include "../src/settings.c"
#include <assert.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

void app_config_effective(const App *a, Config *out) { *out = a->cfg; }
int app_config_apply(App *a, const Config *config, int save, char *error, size_t cap)
{
    if (config_validate(config, error, cap) < 0) return 0;
    if (save && config_save(config, a->config_path, error, cap) < 0) return 0;
    a->cfg = *config;
    return 1;
}
void app_status(App *a, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(a->status, sizeof a->status, format, args);
    va_end(args);
}

/* This suite intentionally targets the single-form credential editor. The
 * application-level profile routing is exercised by the live PTY suite. */
App *client_get_app(Client *c, size_t i, char *e, size_t n)
{ (void)c; (void)i; (void)e; (void)n; assert(!"Unexpected profile lookup"); return NULL; }
void client_new_config(const Client *c, Config *out)
{ (void)c; (void)out; assert(!"Unexpected new-profile form"); }
int client_add(Client *c, const Config *v, size_t *i, char *e, size_t n)
{ (void)c; (void)v; (void)i; (void)e; (void)n; assert(!"Unexpected profile add"); return -1; }
int client_connect(Client *c, size_t i, char *e, size_t n)
{ (void)c; (void)i; (void)e; (void)n; assert(!"Unexpected profile connect"); return -1; }
int client_interface_apply(Client *c, const Config *v, int save, char *e, size_t n)
{ (void)c; (void)v; (void)save; (void)e; (void)n; assert(!"Unexpected interface apply"); return -1; }

static void text(int y, int x, int width, const char *value, attr_t attrs)
{
    if (y < 0 || y >= LINES || x < 0 || x >= COLS || width <= 0) return;
    if (width > COLS - x) width = COLS - x;
    (void)attr_set(attrs, (short)PAIR_NUMBER(attrs), NULL);
    (void)mvaddnstr(y, x, value, width);
    attrset(A_NORMAL);
}

static int contains(const void *data, size_t size, const void *needle, size_t length)
{
    for (size_t i = 0; length <= size && i <= size - length; ++i)
        if (!memcmp((const char *)data + i, needle, length)) return 1;
    return 0;
}

static void no_screen_secret(const char *secret)
{
    char row[256];
    for (int y = 0; y < LINES; ++y) {
        assert(mvinnstr(y, 0, row, COLS) != ERR); row[COLS] = 0;
        assert(!strstr(row, secret) && !strstr(row, "DO-NOT-REVEAL"));
    }
}

int main(void)
{
    FILE *output = tmpfile(), *input = tmpfile();
    assert(output && input);
    SCREEN *screen = newterm("xterm-256color", output, input);
    assert(screen && resizeterm(30, 120) != ERR);
    vipirc_focus_colors();
    char directory[] = "/tmp/vipirc-secret-fields-XXXXXX";
    assert(mkdtemp(directory));
    App *a = calloc(1, sizeof *a);
    Settings *s = settings_create();
    assert(a && s);
    config_defaults(&a->cfg);
    assert(snprintf(a->config_path, sizeof a->config_path, "%s/config.ini", directory) > 0);
    const char *old = "DO-NOT-REVEAL-previous-password";
    strcpy(a->cfg.password, old);
    strcpy(a->cfg.server_password, "DO-NOT-REVEAL-server-password");
    settings_open(s, a);
    s->selected = 12;
    assert(fields[12].kind == FIELD_SECRET && fields[15].kind == FIELD_SECRET);
    char value[256]; field_value(s, 12, value, sizeof value);
    assert(!strcmp(value, "(set; hidden)"));
    settings_draw(s, a, text); no_screen_secret(old);
    char field_row[256];
    assert(mvinnstr(4 + (int)(s->selected - s->first), 0, field_row, COLS) != ERR);
    field_row[COLS] = 0;
    assert(strstr(field_row, "[Plaintext password: (set; hidden)]"));
    assert(field_row[COLS - 1] == ' '); /* no stray far-right focus bracket */
    edit_field(s);
    assert(s->editing && s->editor.selection_all);
    settings_draw(s, a, text); no_screen_secret(old);
    char row[256]; assert(mvinnstr(s->edit_top, s->edit_left, row, s->edit_width) != ERR);
    assert(row[0] == '*' && row[1] == '*');
    settings_key(s, a, 'X', 0);
    assert(s->editor.len == 1 && s->editor.data[0] == L'X');
    const wchar_t previous[] = L"DO-NOT-REVEAL";
    assert(!contains(&s->editor, sizeof s->editor, previous, sizeof previous - sizeof(wchar_t)));
    commit_field(s);
    assert(!s->editing && !strcmp(s->work.password, "X") && !s->editor.len);
    settings_close(s, a);
    for (size_t i = 0; i < sizeof *s; ++i) assert(!((unsigned char *)s)[i]);
    assert(!strcmp(a->cfg.password, old));

    settings_open(s, a); s->selected = 12; edit_field(s);
    settings_begin_paste(s); assert(settings_pasting(s));
    settings_key(s, a, 'P', 0);
    settings_end_paste(s); assert(!settings_pasting(s));
    settings_close(s, a);
    assert(!contains(s, sizeof *s, old, strlen(old)));

    settings_open(s, a); s->selected = 8; edit_field(s);
    assert(s->choosing);
    settings_draw(s, a, text);
    assert(mvinnstr(s->choice_top + 1, s->choice_left, field_row, s->choice_width) != ERR);
    field_row[s->choice_width] = 0;
    assert(strstr(field_row, "(*) none"));
    assert(s->choice_width == 44 && s->choice_left > 0 &&
           s->choice_left + s->choice_width < COLS);
    chtype border = mvinch(s->choice_top + 1, s->choice_left + s->choice_width - 1);
    assert((border & A_CHARTEXT) == (ACS_VLINE & A_CHARTEXT) && (border & A_REVERSE));
    chtype selected = mvinch(s->choice_top + 1, s->choice_left + 2);
    chtype unselected = mvinch(s->choice_top + 2, s->choice_left + 2);
    assert((selected & A_BOLD) && (selected & A_UNDERLINE) && !(selected & A_REVERSE));
    assert((unselected & A_REVERSE) && !(unselected & A_UNDERLINE));
    assert(mvinnstr(s->choice_top + 1, 0, field_row, COLS) != ERR);
    field_row[COLS] = 0;
    assert(field_row[COLS - 1] == ' '); /* the popup border ends at its own width */
    no_screen_secret(old);
    settings_close(s, a);

    const char *literal = "  #;= DO-NOT-REVEAL literal  ";
    settings_open(s, a); s->selected = 12; edit_field(s);
    assert(ed_from_utf8(&s->editor, literal)); commit_field(s);
    assert(!strcmp(s->work.password, literal));
    settings_draw(s, a, text); no_screen_secret(literal);
    s->action = FORM_SAVE; perform_action(s, a);
    assert(!settings_active(s));
    for (size_t i = 0; i < sizeof *s; ++i) assert(!((unsigned char *)s)[i]);
    Config loaded;
    char error[512]; config_defaults(&loaded);
    assert(config_load(&loaded, a->config_path, error, sizeof error) == 1);
    assert(!strcmp(loaded.password, literal));
    struct stat st; assert(!stat(a->config_path, &st) && (st.st_mode & 0777) == 0600);
    config_wipe(&loaded, sizeof loaded);

    settings_open(s, a); s->selected = 0; edit_field(s);
    assert(ed_from_utf8(&s->editor, "different.invalid")); commit_field(s);
    assert(!s->work.password[0] && !s->work.server_password[0] &&
           !s->work.password_gpg_file[0] && !s->work.server_password_gpg_file[0]);
    settings_close(s, a);
    assert(!unlink(a->config_path) && !rmdir(directory));
    config_wipe(a, sizeof *a); free(a); settings_destroy(s);
    endwin(); delscreen(screen); fclose(output); fclose(input);
    puts("credential fields: masked list/editor, replacement/cancel/save wiping, literal-space save/load and changed-server reset passed");
    return 0;
}
