/* Reuse the real offline command fixture, then export its localized reader
 * output so the Python check can compare every source help line byte-for-byte.
 * This exercises the runtime emit/sanitize path, including longer UTF-8 prose. */
#define main original_idle_commands_test_main
#include "test_idle_commands.c"
#undef main
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(setenv("ONEKEYIRC_LANG", argv[1], 1) == 0);
    assert(setenv("VIPIRC_LANG", argv[1], 1) == 0);
    Fixture fixture;
    init(&fixture, 1);
    fixture.commands.translate = irc_tr;
    assert(run(&fixture, "/help all") == IDLE_COMMAND_DONE);
    assert(fwrite(output, 1, output_used, stdout) == output_used);
    cleanup(&fixture);
    return ferror(stdout) ? 1 : 0;
}
