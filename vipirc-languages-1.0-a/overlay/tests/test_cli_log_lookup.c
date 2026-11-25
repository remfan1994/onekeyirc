#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"
#include <stdio.h>
int main(void)
{
    puts(irc_tr("In log view, / opens Command; ordinary typing opens Compose."));
    puts(irc_tr("Enter sends by default; [input] send_mode=review adds a review step."));
    puts(irc_tr("Esc leaves an editor/reader; in Review it restores the draft; in the log it backgrounds."));
    puts(irc_tr("Use signals is off by default: Ctrl-C/Ctrl-Z cancel a draft, not the program; Ctrl-D is inert."));
    puts(irc_tr("[input] use_signals=false is the default; true enables tty Ctrl-C/Ctrl-Z signals."));
    puts(irc_tr("Tab completes. Command Up recalls accepted commands; Compose arrows edit."));
    puts(irc_tr("Start with /connect irc.example.org, then /join #channel."));
    puts(irc_tr("/saveconf remembers your settings; next launch, /connect reuses them."));
    puts(irc_tr("Optional guided setup: /quickstart or /panel."));
    puts(irc_tr("Optional hotkeys: /bindings, /bind KEY ACTION, /unbind KEY|all; none by default."));
    puts(irc_tr("Help: /help ls lists topics; /help COMMAND explains a command."));
    puts(irc_tr("Session controls use job numbers from --sessions, not process IDs."));
    puts(irc_tr("Locale fallback: LC_ALL, LC_MESSAGES, LANG; IRC commands stay in English."));
    puts(irc_tr("Persistent logging unavailable for this server:"));
    puts(irc_tr("Cannot open the private log file"));
    puts(irc_tr("existing log file is not an owned 0600 regular file"));
    puts(irc_tr("log file is busy or cannot be locked"));
    puts(irc_tr("16 MiB log limit reached; archive the log and toggle logging off/on"));
    puts(irc_tr("write failed"));
    puts(irc_tr("Log directory must belong to you and have exact mode 0700"));
    puts(irc_tr("Log filename exceeds its byte limit"));
    puts(irc_tr("Cannot open private log file:"));
    puts(irc_tr("Existing log file is not an owned 0600 regular file within the 16 MiB limit"));
    return 0;
}
