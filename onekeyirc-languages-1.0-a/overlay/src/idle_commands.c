/* Original command interface. IRC text and configuration values are never shell input.
 *
 * Reading route: catalog/topics -> word/alias helpers -> help rendering ->
 * profile/preference operations -> idle_command dispatch -> recall filtering.
 * This module reports requested frontend actions; the frontend owns dialogs,
 * review acceptance and terminal changes. Help is maintained here and exported
 * by scripts/sync_docs.py, not edited in the generated minimalist manuals. */
#include "idle_commands.h"
#include "l10n.h"
#include "display_l10n.h"
#include "config_number_l10n.h"
#include "commands.h"
#include "summary.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

typedef struct Command {
    IdleCommandSpec spec;
    const char *detail;
    const char *examples;
} Command;

/* One table is the supported-command inventory, completion source and topic help. */
static const Command catalog[] = {
    {{"autoauth", "/autoauth [status|off|sasl USER file PATH|sasl USER gpg PATH]", "Inspect or prepare SASL authentication"},
     "Changes the selected server's authentication for its next connection. Verified TLS is required.\n"
     "With no argument or status, show connection/authentication state and configured credential file paths, never password contents.\n"
     "Current and pending settings are distinguished. Long paths are complete labeled continuation lines.\n"
     "PATH is the remaining literal text, including spaces; no shell expansion or quoting is performed.\n"
     "No file is opened by this command. GPG keys must be unlocked outside the program.\n"
     "Plaintext passwords can be configured explicitly in a private config file, never as command arguments.\n"
     "Use /panel for a bouncer profile; /autoauth changes are refused there because its login identity and credential source are managed together.\n"
     "Use /save to persist. Disconnect first, then /connect to use applied changes; an active connection is never retargeted.",
     "/autoauth status\n/autoauth sasl myaccount gpg /home/me/irc-account.gpg\n/autoauth off\n/save"},
    {{"autocommands", "/autocommands [list|add COMMAND|remove NUMBER|clear|on|off|delay SECONDS|interval SECONDS]", "Prepare delayed commands for the next connection"},
     "Commands belong to the selected server. They run once after registration and required SASL succeeds.\n"
     "The default delay is 15 seconds, with at least 2 seconds between completed commands.\n"
     "Use delay 0..300 and interval 2..60 to change those timings. A stalled connection never triggers a catch-up burst.\n"
     "Supported commands: /msg, /join, /nick, /away, /back and /whois, including their documented exact aliases.\n"
     "Up to 16 commands of 511 UTF-8 bytes each. One command must fit one IRC message; it is never silently split.\n"
     "There is no shell, raw IRC or current-conversation expansion. Automatic joins do not change the selected view.\n"
     "Changes apply to the next connection. /cancel stops still-unsent startup commands on this connection.\n"
     "Lists redact message text and join keys; automatic commands are excluded from history and local message echoes.\n"
     "Save with /save. The file must be private (0600 or 0400); an enabled nonempty list requires verified TLS.\n"
     "Test mode never runs automatic commands. Startup command text is stored literally in the config, not encrypted by GPG.",
     "/autocommands add /msg ChanServ op #chat mynick\n/autocommands delay 15\n/autocommands interval 2\n/autocommands list\n/save"},
    {{"autoconnect", "/autoconnect [on|off]", "Inspect or change automatic startup connection"},
     "Without an argument, show the selected server's setting. Changes affect the next program startup.\n"
     "Use /connect for an immediate connection; use /save to persist the startup choice.\n"
     "Each saved server has its own toggle, auto-join channels and authentication settings.",
     "/autoconnect\n/autoconnect on\n/save"},
    {{"autojoin", "/autojoin [list|add #CHANNEL|remove #CHANNEL|clear]", "Inspect or prepare startup channel joins"},
     "With no argument or list, show the selected server, registration/authentication state, running list and pending list.\n"
     "Edit the selected server's auto-join list for its next explicit /connect. Add/remove accepts one channel, without a key.\n"
     "Changes are validated before application and do not join or part a live channel.\n"
     "Automatic retries keep the running list. Disconnect first, then /connect to apply a pending list.\n"
     "Use /join or /part for the running connection, and /save to persist startup changes.\n"
     "Bouncer profiles restore their own memberships; /autojoin changes are refused there. Use /join and /part instead.",
     "/autojoin list\n/autojoin add #ircnow\n/autojoin remove #old\n/save"},
    {{"away", "/away [MESSAGE]", "Set away status, or clear it without a message"},
     "Requires registration. The server confirms the resulting away state. MESSAGE is literal text.",
     "/away Back after lunch\n/away"},
    {{"back", "/back", "Clear away status"},
     "Equivalent to /away with no message. Requires a registered connection.", "/back"},
    {{"background", "/background", "Keep connections running and release the terminal"},
     "Detaches the running session after restoring the terminal. The shell receives this application's numbered resume command.\n"
     "From the shell, --sessions lists this program's attached and detached private jobs; --resume resumes its sole available detached job.\n"
     "Use --resume NUMBER when more than one job exists. NUMBER is the job ID, not a process ID; it stays stable until exit and may then be reused.\n"
     "--detach-session NUMBER releases an attached terminal/controller but keeps the worker, IRC connections, drafts and history alive for --resume NUMBER.\n"
     "--close-session NUMBER ends the whole job, all IRC connections and transfers, and its unsaved drafts and in-memory history. Success is reported only after verified completion.\n"
     "If resume is busy after a lost SSH terminal, list jobs, detach that job, then resume it. See /help sessions for the recovery sequence.\n"
     "An unresponsive worker or a legacy worker lacking a requested control can fail. Installing a binary does not update a running worker; restart it deliberately after an upgrade.\n"
     "Resume redraws retained in-memory scrollback even when no chat arrived while detached. /set single_session on refuses a second background job so bare --resume remains unambiguous.\n"
     "To start without a terminal, use --background from the shell; see --help for this program's options.\n"
     "When background_away is on and this profile permits automatic away, /background or managed terminal loss queues the configured AWAY after detach. Resume clears only that client-generated away state; a manual /away is preserved.\n"
     "Add --reconnect to retry transient connection failures. See /help deployment and /help reconnect.\n"
     "No terminal multiplexer is required. A pending resolver or credential helper can require a retry.\n"
     "Suspending an ordinary foreground process pauses its networking. A resumed job can suspend its foreground controller while its managed worker continues networking.", "/background"},
    {{"bind", "/bind KEY ACTION", "Assign a custom key to a local program function"},
     "No custom bindings are installed by default. Use /bindings keys and /bindings actions for the supported names.\n"
     "The new assignment replaces that key's previous custom action for every server. Other bindings are preserved.\n"
     "Bindings run in the main chat and command prompt, not in paste, password/setup fields, readers or confirmation screens.\n"
     "Changes take effect now but remain unsaved until /save input. Existing navigation works when a key has no active custom binding.\n"
     "Only named local functions are accepted. There is no arbitrary command, macro, send action or shell expansion.\n"
     "Terminal-reserved keys may never reach the program; binding a key does not change terminal signal assignments.",
     "/bindings actions\n/bind F7 summary\n/bind Alt-c command\n/save input"},
    {{"bindings", "/bindings [actions|keys]", "List custom bindings or available actions and keys"},
     "Without arguments, list your current application-wide bindings. An empty list means none are assigned.\n"
     "Use /bind to assign or replace a key, /unbind KEY to remove it, and /unbind all to clear the list.\n"
     "F1..F12, Ctrl-A..Ctrl-Z, Alt-a..Alt-z, Alt-0..Alt-9 and named navigation keys are supported.\n"
     "Ctrl-H is Backspace, Ctrl-I is Tab, Ctrl-J and Ctrl-M are Enter, and Ctrl-[ is Escape; these are the same binding.\n"
     "Alt letters use unshifted lowercase terminal bytes. Function/Alt key delivery depends on the terminal and any multiplexer.\n"
     "Up to 64 bindings are saved as KEY=action in [hotkeys] in the primary config. /save input persists command edits.\n"
     "See /help hotkeys for scope, draft protection and terminal signal behavior.",
     "/bindings\n/bindings keys\n/bindings actions\n/help hotkeys"},
    {{"cancel", "/cancel", "Cancel queued chat and startup commands"},
     "Removes chat messages waiting for pacing and stops pending automatic commands on the selected server.\n"
     "A frame already being written is allowed to finish.\n"
     "Cancellation does not retract messages that the server has already received.", "/cancel"},
    {{"catchup", "/catchup", "Read this conversation from your most recent sent message"},
     "Starts at your most recent locally sent chat/action in the selected channel or query. Server/mentions views have no sent-chat anchor.\n"
     "An eligible private log is tried first. Logging must already have been enabled when the anchor was sent and remain healthy; sensitive authentication text is not a disk anchor. Otherwise retained in-memory history is used when its anchor is still available.\n"
     "The disk reader is a stable snapshot: Up/Down move one row, Page Up/Page Down a page, Home returns to the anchor, End shows the newest snapshot page, and Escape returns live. New traffic does not move that snapshot.\n"
     "The in-memory fallback opens ordinary retained scrollback at the anchor: Home reaches the oldest retained page, and End or Escape returns live. Older retained messages can still be browsed.\n"
     "A replaced or changed anchor record cannot be used as the original disk anchor. A fresh process does not restore anchors from old logs. See /help logging and /help history.", "/catchup"},
    {{"channel", "/channel NUMBER|#CHANNEL", "Select a channel; number 0 selects the server"},
     "Numbers are listed by /channels. Selection changes the viewed conversation and subsequent plain-text target.\n"
     "It does not join a channel. A retained parted channel must be rejoined before sending.",
     "/channels\n/channel 0\n/channel #ircnow"},
    {{"channels", "/channels", "List channels on the selected server"},
     "Channel 0 is always the server message/MOTD view. Queries have their own /ql list.\n"
     "Rows show joined/parted state and unread/mention counts.", "/channels\n/channel 1"},
    {{"cl", "/cl", "List channels (alias of /channels)"},
     "Lists this server's channel views, including channel 0 for server messages and each channel's joined/parted state.\n"
     "Use /channel NUMBER or /channel #NAME to select one. Queries have their own /ql list.",
     "/cl\n/channel 1"},
    {{"clear", "/clear", "Clear retained history for the selected view"},
     "Clears this view's bounded in-memory history. It does not part, disconnect, retract sent text or delete an existing persistent log.\n"
     "Terminal scrollback already produced by the program is controlled by the terminal.\n"
     "In the main chat view, Up opens one-row retained history, Page Up opens a page, and Home jumps to the oldest retained page; Down/Page Down move newer. "
     "New matching traffic does not move the history page being read. End or Escape returns to the live tail; navigation keys do not open the composer.", "/clear"},
    {{"close", "/close [NAME|NUMBER]", "Close a channel or query view"},
     "Without an argument, close the current conversation. A channel is parted before its view is removed.\n"
     "The permanent server and mentions views cannot be closed. Channel numbers come from /channels.",
     "/close alice\n/close #old"},
    {{"connect", "/connect [-tls|-plain] [-nick NICK] HOST [PORT] | /connect", "Connect directly to a server, or reuse the saved connection"},
     "Start with /connect irc.example.org. No profile, alias or server number needs to be created first.\n"
     "A new server uses verified TLS on port 6697 and your system login nickname, or user when that login is unsuitable.\n"
     "An existing matching server keeps its configured port, transport, nickname and authentication.\n"
     "Use -nick NICK for an explicit nickname, or /nick NICK before connecting. Passwords are not positional arguments.\n"
     "Use -plain for explicitly unencrypted IRC (new-server default port 6667). -tls and -ssl explicitly select verified TLS.\n"
     "A +PORT also selects TLS. An ordinary numeric port never disables TLS. --tls, --plain and --nick are accepted too.\n"
     "The connection becomes selected and opens its server/MOTD view. An already active matching connection is selected without duplication.\n"
     "A different endpoint gets fresh authentication settings; another server's credentials and channels are not copied.\n"
     "Bare /connect uses the selected server, including applied settings for the next connection. /saveconf remembers the selected default for next launch.\n"
     "For bouncer profiles sharing one host, select the intended profile with /server NUMBER, then use bare /connect.\n"
     "Existing server numbers are also accepted for compatibility. Test mode remains offline; configuration edits require an explicit save.",
     "/connect irc.example.org\n/join #chat\n/saveconf\n/connect\n/connect -nick mynick irc.example.org +6697\n/connect -plain localhost 6667"},
    {{"dcc", "/dcc [list|NICK PATH|send [-address IPV4] [-port PORT] NICK PATH|accept NUMBER [DIRECTORY]|reject NUMBER|cancel NUMBER]", "Offer, review and manage direct file transfers"},
     "With no arguments, list transfers. /dcc NICK PATH offers a file on the selected server.\n"
     "PATH is the remaining literal text, including spaces; it is relative to this process's launch directory.\n"
     "The client never changes that directory, including while browsing files or running in the background.\n"
     "The short send form advertises the local IPv4 address of the IRC connection. Behind NAT, use -address with your reachable IPv4 and -port with a forwarded port.\n"
     "No external address-discovery request is made. IPv6-only IRC connections need an explicit IPv4 override.\n"
     "DCC is an unencrypted direct connection. The peer receives your advertised IP; accepting an offer reveals your IP to that peer.\n"
     "Incoming offers stay unopened until /dcc accept NUMBER. DIRECTORY defaults to the launch directory and must be owned by you and private.\n"
     "No existing destination is overwritten; symlinks and special files are refused. Offline test mode does no transfer file/socket work.\n"
     "Use /dcc reject NUMBER for an incoming offer, or /dcc cancel NUMBER for an active transfer.\n"
     "Transfer numbers are shared across this process's servers. Closing an originating server cancels its transfers; disconnecting IRC does not cancel an accepted transfer.",
     "/dcc alice file.txt\n/dcc list\n/dcc accept 1 ./downloads\n/dcc send -address 203.0.113.20 -port 5000 alice file.txt\n/dcc cancel 2"},
    {{"disconnect", "/disconnect [SERVER_NUMBER]", "Disconnect a server while retaining its profile"},
     "Without a number, disconnect the selected server. History and configuration remain available.\n"
     "This does not change its next-startup autoconnect setting.", "/disconnect\n/disconnect 1"},
    {{"help", "/help [TOPIC|COMMAND]", "Read topic help, syntax and practical examples"},
     "Use /help ls for the topic index, /help commands for command entry and its inventory, or /help autoconnect for one command.\n"
     "Request one topic or command name, without arguments. /help /ls explains the local list command; /help ls lists help topics. A leading slash is otherwise optional.\n"
     "/help all opens the complete manual in the separate help reader.",
     "/help\n/help ls\n/help commands\n/help autoconnect\n/help /ls"},
    {{"ignore", "/ignore [list|clear|[add|remove] MASK]", "Manage local ignored senders"},
     "Accepts a nickname or nick!user@host mask with * and ? wildcards. Matching user messages/notices are hidden locally.\n"
     "Ignored text does not create queries or mentions. Protocol state, server notices and membership still work.\n"
     "Use /save to persist this server's masks.", "/ignore list\n/ignore add noisy!*@*\n/ignore remove noisy!*@*"},
    {{"join", "/join #CHANNEL [KEY]", "Join one channel on the selected server"},
     "Requires registration. A key is one printable ASCII token and is never saved or retained in command history.\n"
     "On a direct connection, the channel name becomes part of the running auto-join list; separately staged /autojoin edits take precedence when saved.\n"
     "Use /autojoin to edit a direct profile's next explicit connection without joining now. A bouncer manages its own memberships.", "/join #ircnow\n/join #private channel-key"},
    {{"list", "/list [MASK] [--min-users N] [--max-users N] [--name TEXT] [--topic TEXT] [--sort channel|users|topic] [--asc|--desc] [--limit N] | /list show [--sort channel|users|topic] [--asc|--desc] [--limit N] | /list clear", "Request and filter the network channel directory"},
     "Requests channels from the selected IRC server; registration is required. MASK is the only filter sent to the server.\n"
     "Minimum/maximum users, name and topic filters are applied locally so they work even when a network lacks extended LIST filtering.\n"
     "--sort selects channel, users or topic; users descending is the default. --limit 0 means no display limit within the bounded cache.\n"
     "/list show prints the cached directory; its --sort, --asc/--desc and --limit options reorder or trim the cache locally without another IRC request. /list clear removes the cache.\n"
     "Disconnect or a server LIST rejection keeps any partial cache but releases the request; retry /list after registration, or use /list clear.\n"
     "The full-screen client opens the sortable channel browser; minimalist mode uses the same options and /list show.\n"
     "Use /ls or /cl for local channel views; /list searches the network.",
     "/list\n/list *linux* --min-users 5 --max-users 500\n/list --topic help --sort users --desc --limit 100\n/list show --sort channel --asc --limit 50\n/list clear"},
    {{"ls", "/ls [servers|channels|queries|unread]", "List server, channel, query or unread entries"},
     "Without an argument, list channels on the selected server. This is a local client list command, not a shell command or a network-wide IRC LIST request.\n"
     "Use /server N, /channel N and /pm NICK to select the corresponding entries.",
     "/ls\n/ls servers\n/ls queries\n/ls unread"},
    {{"manual", "/manual", "Read the complete built-in manual"},
     "The same topic and command help is available without connecting. /help TOPIC is the shorter route.", "/manual"},
    {{"me", "/me TEXT", "Send an action to the selected conversation"},
     "Requires a joined channel or query and a registered connection. Message-budget and send-mode rules apply.\n"
     "The frontend reviews a split message before queueing it.", "/me waves"},
    {{"mentions", "/mentions [clear]", "View mentions or clear retained mentions across servers"},
     "Selects this server's mentions view. It contains bounded recent source lines and their original timestamps.\n"
     "/mentions clear removes retained mention copies across all current servers without switching views.\n"
     "Source conversation history, unread counts and pending work are preserved.\n"
     "Use /watch for additional literal strings; /channel or /pm returns to a sendable conversation.", "/mentions\n/mentions clear\n/channel #ircnow"},
    {{"msg", "/msg TARGET TEXT", "Send text to one nickname or channel"},
     "Requires registration. This command creates a send plan; direct/review mode and split-message consent still apply.\n"
     "It does not silently change the current send target. Service/credential messages are excluded from command history.",
     "/msg alice Hello\n/msg #ircnow Good evening"},
    {{"next", "/next", "Select the next conversation on this server"},
     "Cycles server, mentions, channel and query views; the internal manual buffer is omitted.", "/next"},
    {{"nick", "/nick NICK", "Set or request the current nickname"},
     "While connected, requests a server-confirmed change. While disconnected, updates the next connection identity.\n"
     "Nicknames use the supported IRC ASCII syntax and are limited to 63 bytes.", "/nick tutankh-aten"},
    {{"panel", "/panel", "Open the interactive configuration panel"},
     "Choose 0 (the default) for application preferences, or profile panels 1..8; panel 1 edits the primary. These are panel choices, not shell job IDs.\n"
     "Application preferences contain history retention, send mode, output format, queue notices and command suggestions for all servers.\n"
     "They work without a configured server. Their final choices are 1 Save (default) and 2 Apply without saving; neither connects.\n"
     "A server panel contains only its connection, startup, authentication, mention and ignore settings.\n"
     "Choosing any panel does not switch your chat conversation.\n"
     "Enter keeps a shown value; '-' clears an optional field. Escape returns one question at a time; repeat it to leave without applying edits.\n"
     "Tab selects the question heading. Press ? there for help about the option and its screen; Tab or Enter resumes the answer.\n"
     "Every editable answer keeps ? as literal text, including the first character and wildcard masks.\n"
     "Watch terms and ignore masks are entered one at a time; an empty new entry finishes that list.\n"
     "Credential input is hidden and setup answers never enter command history. Existing credential files are not read while editing.\n"
     "For a GPG source, choose Create new or Use existing. Creation offers a private default path; Enter accepts it and prepares its directory (0700).\n"
     "A different destination must be in an existing directory you own with mode 0700. The path is checked before the key and password questions.\n"
     "Creation then asks for a full local public-key fingerprint and the password twice. Existing files and symlinks are never replaced.\n"
     "Credential paths are stored as absolute paths. /autoauth status shows their complete configured locations without reading their contents.\n"
     "Encryption starts only after Finish, keeps IRC responsive, and never uses a plaintext temporary file. Escape cancels pending work.\n"
     "A failed config save keeps completed ciphertext available for a deliberate retry; files referenced by a saved config are preserved.\n"
     "Test mode does not start GPG or create credential files. Unlock the matching private key externally before authenticating.\n"
     "A server panel's final choices are 1 Save and connect, 2 Save only (default), 3 Connect once, or 4 Apply only.\n"
     "Nothing is applied, saved or connected merely by opening the panel or answering its earlier questions.\n"
     "Connect once applies without saving; Apply only neither saves nor connects. A live connection is never implicitly retargeted or reconnected.\n"
     "A failed connection does not undo changes already applied or saved by the final action.\n"
     "Use /quickstart for the shorter server, transport, port and nickname setup.", "/panel"},
    {{"part", "/part [#CHANNEL] [MESSAGE]", "Leave a channel with an optional reason and retain its history"},
     "Without a channel, parts the selected joined channel. Text after a channel is the PART reason; text without a channel is the reason for the selected channel.\n"
     "If no explicit reason is supplied, part_message is used when configured. The running auto-join entry is removed.\n"
     "Queued messages for that channel are cancelled; one in-flight frame may finish. Use /close to remove the view.",
     "/part\n/part Going offline\n/part #old Moving elsewhere"},
    {{"pm", "/pm NICK|NUMBER", "Select or open a private query without sending"},
     "Names open or select a query. Numbers select an existing query from this server's /ql list.\n"
     "Creating a query does not contact the nickname until a message is sent. Numbers can change after closing a query.",
     "/pm alice\n/ql\n/pm 1"},
    {{"prev", "/prev", "Select the previous conversation on this server"},
     "The reverse of /next. It does not connect, join or send.", "/prev"},
    {{"ql", "/ql", "List private queries on the selected server"},
     "Query numbers start at 1 and are separate from channel numbering. Use /query NICK or /query NUMBER to select one.\n"
     "Use /close NICK to close a query; run /ql again after closing because subsequent query numbers may change.",
     "/ql\n/query 1\n/query alice"},
    {{"queries", "/queries", "List private queries (alias of /ql)"},
     "Uses the same private query list as /ql. Channel numbering remains separate.", "/queries\n/pm alice"},
    {{"query", "/query NICK|NUMBER", "Open or select a private conversation"},
     "Selects a private conversation without sending a message. Type normally to compose to that nickname.\n"
     "Use /ql to list numbered queries and /close NICK to close one. /pm accepts the same names and numbers.\n"
     "A number must refer to an existing query on the selected server; 0 never selects or creates a query.",
     "/query alice\n/ql\n/query 1\n/close alice"},
    {{"quickstart", "/quickstart", "Open guided connection setup"},
     "Edit the current profile's server, TLS transport, port and nickname in that order, then review the settings.\n"
     "Verified TLS is the default. A plaintext connection must be selected explicitly.\n"
     "Enter keeps a shown value. Escape returns one question at a time; repeat it to leave without applying edits. Answers never enter command history.\n"
     "At the final review choose 1 Save and connect, 2 Save only, 3 Connect once (default), or 4 Apply only.\n"
     "Opening setup and answering earlier questions does not apply settings, save a file or connect.\n"
     "Connect once applies without saving; Apply only neither saves nor connects. A live connection is never implicitly retargeted or reconnected.\n"
     "A failed connection does not undo changes already applied or saved by the final action.\n"
     "Use /panel for bouncer profiles and broader configuration choices. Quickstart does not edit an existing bouncer login.", "/quickstart"},
    {{"quit", "/quit [MESSAGE]", "Exit the program and close all sessions"},
     "Sends MESSAGE as the best-effort IRC QUIT reason on each live server, or quit_message when MESSAGE is omitted, then restores the terminal and exits.\n"
     "Use /background to leave connections running instead.", "/quit\n/quit Client restart"},
    {{"save", "/save [input|output|all]", "Save the current server, application preferences, or all profiles"},
     "Bare /save writes the selected server's effective settings, including staged connection changes.\n"
     "/save input and /save output both write the primary effective profile, including application preferences and staged primary connection settings. They do not save only the named subset.\n"
     "/save all saves each healthy profile separately. If one fails, earlier successful files remain saved and failures are reported.\n"
     "Each file replacement is private and atomic; this is not one transaction across all files. Test mode never writes configs.",
     "/save\n/set send_mode review\n/save input\n/save all"},
    {{"saveconf", "/saveconf", "Save configuration and remember the selected connection"},
     "Saves application preferences and all healthy server configurations, then remembers the selected server as the default.\n"
     "Next time you start the program, /connect uses that saved default; no server number or separate setup command is needed.\n"
     "Nickname, joined channels, authentication and startup choices are included; explicit staged connection edits take precedence.\n"
     "This does not turn on automatic startup connection. Use /autoconnect on first if you want that behavior.\n"
     "Each file is replaced privately and atomically. Successful writes remain saved if another file fails.\n"
     "The selected default is remembered if its own save succeeds; failures are reported and unreadable profiles are not overwritten.\n"
     "Test mode never writes configuration. /save retains its more targeted forms for individual profiles and preferences.",
     "/connect irc.example.org\n/join #chat\n/saveconf\n/quit"},
    {{"server", "/server [-tls|-plain] [-nick NICK] HOST [PORT] | /server [NUMBER|add HOST [PORT] [NICK]|connect NUMBER|disconnect NUMBER|close NUMBER]", "Connect to a server, or select an existing one"},
     "With a hostname, this is the same direct connection command as /connect. No prior profile creation is needed.\n"
     "No argument selects the current server's message/MOTD view. NUMBER selects a server slot without connecting it.\n"
     "add creates a profile with fresh credentials, verified TLS and autoconnect off; it neither selects, connects nor saves it.\n"
     "PORT defaults to 6697. A different port does not disable TLS; plaintext must be explicitly configured.\n"
     "connect/disconnect do not change selection. close removes a secondary session/catalog entry, not its profile file.\n"
     "Minimalist server numbers are 1..8, with primary 1. The primary remains available; disconnect it instead of closing it.\n"
     "Numbers remain stable for the lifetime of their sessions. Panel 0 is reserved for application preferences.",
     "/server irc.example.org\n/join #chat\n/saveconf\n/servers\n/server 1"},
    {{"servers", "/servers", "List all configured server sessions"},
     "Shows minimalist server slots 1..8 (primary 1), selected server, connection state and per-server autoconnect choice.\n"
     "A loaded profile stays available even when it is not connected.", "/servers\n/server 1"},
    {{"set", "/set send_mode direct|review | /set composer_mode line|fullscreen | /set output_format compact|full | /set queue_notices on|off | /set program_messages on|off | /set persistent_logging on|off | /set single_session on|off | /set default_interface idle|ncurses | /set command_suggestions alphabetical|common|history|frequent|contextual | /set part_message [TEXT] | /set quit_message [TEXT] | /set background_away on|off | /set background_away_message TEXT | /set autoaway_minutes 0..1440 | /set autoaway_message TEXT", "Choose application-wide input, output and presence preferences"},
     "direct is the default: Enter submits the composition, with mandatory consent if it needs multiple IRC messages.\n"
     "review adds a deliberate review step before ordinary messages are queued.\n"
     "output_format compact is the default: selected chat shows [HH:MM:SS] <nick> text.\n"
     "output_format full includes the server, date and target on each chat line. Mentions and other-server diagnostics retain necessary origin context in compact mode.\n"
     "queue_notices off hides routine queued/paced acknowledgements. In the minimalist frontend, an explicitly reviewed send omits its redundant routine queue acknowledgement even when this setting is on. Errors and multi-message confirmation always remain visible.\n"
     "program_messages off hides low-priority local feedback such as draft-cancelled, input-cleared and preserved-draft notices. Errors and safety confirmations remain visible.\n"
     "persistent_logging on appends retained server/conversation lines to an owned 0600 .log file in the saved configuration's owned 0700 directory. It is off by default. Each log stops at 16 MiB; archive it and toggle logging off/on to resume. Unsafe files or a busy log stop logging with an error; live chat continues.\n"
     "single_session on permits only one background job in this client namespace, so a bare --resume is unambiguous. Multiple IRC servers remain available inside that session.\n"
     "default_interface controls the next fresh launch where interface selection is supported. It does not switch the current interface. /save input persists this application preference.\n"
     "command_suggestions chooses alphabetical order, common commands, recent command history, most-used commands, or commands relevant to the originating screen.\n"
     "composer_mode line keeps one input row below the live tail; fullscreen keeps a multi-line editor while reserving a live-tail pane. Line is the default.\n"
     "part_message and quit_message provide default IRC departure reasons; an explicit /part or /quit message overrides them. Empty part_message or quit_message disables that default text.\n"
     "background_away sends background_away_message after a successful /background or managed terminal loss. autoaway_minutes sets local-idle auto-away; 0 disables it.\n"
     "Only client-generated away state is cleared automatically on resume or genuine local input. A manual /away is never overwritten or cleared by these features.\n"
     "Common is the default. Contextual prioritizes channel, query, server or client commands while retaining matching commands from other groups.\n"
     "Common uses a curated useful order. Contextual remembers the actual screen that opened command entry, including a client-wide summary reader.\n"
     "These are application preferences and apply across servers. Use /save input or /save output to persist them.\n"
     "/panel, followed by 0, edits these preferences and the global history limit together.",
     "/set send_mode review\n/set composer_mode line\n/set command_suggestions contextual\n/save input\n/set output_format full\n/set queue_notices off\n/set program_messages off\n/set persistent_logging on\n/set single_session on\n/save output"},
    {{"settings", "/settings", "Show effective settings with credentials redacted"},
     "Shows application preferences separately from the selected server's pending connection choices.\n"
     "Authentication values are reported by source type; passwords and credential paths are never printed.\n"
     "Use /autoconnect, /autojoin, /autoauth and /set to change their respective settings.", "/settings\n/help configuration"},
    {{"sl", "/sl", "List servers (alias of /servers)"},
     "Uses the same configured-server list and stable slot numbers as /servers.\n"
     "Use /server NUMBER to select one.", "/sl\n/server 1"},
    {{"soju", "/soju [COMMAND]", "Control the selected soju bouncer"},
     "Requires a connected soju profile configured in /panel. No argument asks BouncerServ for help.\n"
     "COMMAND is sent as one paced private message, without local echo or command history.\n"
     "A direct IRC connection cannot use this shortcut. Replies appear in the server view.\n"
     "Use the bouncer's own help for account and network commands.", "/soju\n/soju network status"},
    {{"summary", "/summary", "Summarize all current server sessions and activity"},
     "Shows every server, channel 0, channel/query views, unread activity, mentions, outgoing queues, startup commands and file transfers.\n"
     "This is a local snapshot and works while disconnected. Press r in its reader, or run it again, to refresh.\n"
     "Opening it does not change your conversation or mark anything read. The copied mentions feed is excluded from activity totals.\n"
     "Mentions may also be unread messages; these totals are not added together. Joined and left channel views are identified separately.\n"
     "The report uses its own scrollable reader and preserves drafts. Slash opens command entry; cancelling returns to the snapshot.\n"
     "A failed refresh retains the previous snapshot. No network request is made by opening or refreshing it.\n"
     "Credentials and the contents of queued messages or automatic commands are never printed.",
     "/summary"},
    {{"topic", "/topic [TEXT]", "Request or change the current channel topic"},
     "Requires a joined channel and registration. Without text, requests the topic; otherwise submits literal TEXT.\n"
     "The server decides whether your current channel privileges permit a change.", "/topic\n/topic Welcome to our channel"},
    {{"transfers", "/transfers [ARGUMENTS]", "Manage file transfers (alias of /dcc)"},
     "The same commands and protections as /dcc. Use /help dcc for send, receive and cancellation syntax.",
     "/transfers\n/transfers alice file.txt"},
    {{"unbind", "/unbind KEY|all", "Remove one or all custom key bindings"},
     "Removing a key restores its ordinary behavior wherever it had a custom action. all clears only the custom binding list.\n"
     "The change applies across servers immediately. Use /save input to retain it after restart.",
     "/unbind F7\n/unbind all\n/save input"},
    {{"unignore", "/unignore MASK", "Remove one local ignore mask"},
     "Equivalent to /ignore remove MASK. Save the selected profile to persist the change.", "/unignore noisy!*@*"},
    {{"unread", "/unread", "List unread conversations across servers"},
     "Rows include the server number, conversation name, unread messages and mention counts.\n"
     "Use /server NUMBER followed by /channel or /pm to select a conversation.", "/unread\n/server 1\n/pm alice"},
    {{"users", "/users [#CHANNEL]", "Print the current or named channel's known users"},
     "This is a textual roster, not a visibility toggle. User mode prefixes are included when known.\n"
     "The roster opens in a separate reader, even when empty. Enter, Escape or q returns to the selected chat log.\n"
     "Up/Down and Page Up/Down scroll the roster. In Help, Enter continues scrolling.\n"
     "An incomplete or capacity-limited NAMES reply is labelled. It does not send WHOIS requests.", "/users\n/users #ircnow"},
    {{"view", "/view NAME|NUMBER", "Select a named conversation or numbered channel"},
     "Channel numbers are listed by /channels; query names by /ql. The server and mentions names are also accepted.\n"
     "Use /help instead of selecting an internal manual buffer.", "/view #ircnow\n/view alice\n/view 0"},
    {{"views", "/views", "List channels (alias of /channels)"},
     "This alias uses channel numbering; private queries remain in /ql.", "/views"},
    {{"watch", "/watch [list|add TEXT|remove TEXT]", "Manage extra literal mention strings"},
     "Nickname mentions always remain enabled. Add/remove treats the remainder as literal text, not a regular expression.\n"
     "Up to 32 additional strings of 1 to 127 UTF-8 bytes are supported per server. Use /save to persist.",
     "/watch list\n/watch add release day\n/watch remove release day\n/mentions"},
    {{"whois", "/whois NICK", "Request nickname information from the server"},
     "Requires registration. Selects the server view so its response is visible. No local command is executed.", "/whois alice"},
    {{"znc", "/znc [COMMAND]", "Control the selected ZNC bouncer"},
     "Requires a connected ZNC profile configured in /panel. No argument asks *status for Help.\n"
     "COMMAND is sent as one paced private message, without local echo or command history.\n"
     "Use /msg *module COMMAND for an enabled ZNC module. Module replies appear in the server view.\n"
     "A direct IRC connection cannot use this shortcut. Use the bouncer's help for its commands.",
     "/znc\n/znc ListNetworks\n/znc Connect"}
};

typedef struct Topic { const char *name, *text; } Topic;
static const Topic topics[] = {
    {"about", "An application of ThatIAm.vip (www.thatiam.vip), designed by remfan1994.\n"
              "License: IALA (Insha-Allah License Agreement); see the included license text.\n"
              "Welcome warning: Not vegetarian? www.bloodguiltcurse.net will change your mind\n"
              "A running session also displays this local reminder once when a frontend is attached:\n"
              "Cow killing is the bloodshed of the innocent. www.bloodguiltcurse.net - ban beef.\n"
              "The reminder has no extra prompt and is not sent to IRC. Reconnection and reattachment do not reset its once-per-worker flag."},
    {"aliases", "Exact alternative names are accepted without ambiguous or fuzzy execution.\n"
              "/conn = /connect; /disc = /disconnect; /detach = /background; /exit = /quit\n"
              "/j = /join; /leave = /part; /message and /privmsg = /msg\n"
              "/nickname = /nick; /names and /nicks = /users; /unaway = /back\n"
              "/channellist = /cl; /querylist = /ql; /serverlist = /servers\n"
              "/configure and /preferences = /panel; /saveconfig = /saveconf\n"
              "/autocommand = /autocommands\n"
              "Completion suggests canonical names; arguments are preserved literally and secret-history filtering is identical."},
    {"authentication", "Use verified TLS with /autoauth sasl USER file PATH or /autoauth sasl USER gpg PATH.\n"
              "The password file or decrypted GPG content supplies the credential; paths are literal.\n"
              "Use /panel to create a new GPG-encrypted credential for an existing local public-key fingerprint.\n"
              "Use an already-unlocked GPG key. No pinentry runs inside the chat editor.\n"
              "Explicit plaintext password= and server_password= are also supported in private config files.\n"
              "Choose only one source for each credential. /settings and /summary keep file locations redacted.\n"
              "/autoauth status shows current and pending credential file locations without reading files or showing passwords.\n"
              "Applied authentication settings take effect on the next disconnected /connect; /save persists them.\n"
              "On a bouncer profile, use /panel to manage the required login and source together; /autoauth changes are refused.\n"
              "Credential files must be owned regular files with mode 0600 or 0400; symlinks and parent traversal are refused.\n"
              "The SASL credential limit is 1024 bytes; ordinary server PASS permits 503 bytes. An assembled ZNC login must also fit its one IRC line.\n"
              "--check-config validates configuration and source selection without reading credentials or checking the login. See /help tls and /help bouncers."},
    {"bouncers", "Use /panel, select a server, then choose connection type znc or soju.\n"
              "Enter the provider's bouncer hostname and TLS port, bouncer username, existing upstream network name and an optional stable client name.\n"
              "Use one saved profile per upstream network. Select the intended profile before bare /connect when several use the same hostname.\n"
              "The client assembles the login identity. ZNC uses the server-password source; soju uses the authentication-password source. Enter only the bouncer password, not a combined login string or a NickServ password.\n"
              "Bouncers restore their joined channels. Local auto-join lists are not replayed and /autojoin edits are refused; use /join and /part to change memberships.\n"
              "Explicit startup commands still run. Review them before enabling a bouncer profile to avoid unwanted repeated joins or service commands.\n"
              "Automatic idle/background away is off for each bouncer profile unless its automatic-away setting is enabled. Manual /away and /back still work.\n"
              "Known pushed history uses original UTC server timestamps, routes outgoing private chat to its recipient, and does not create fresh mentions, execute CTCP/DCC or mutate live state.\n"
              "History storage stays bounded. There is no on-demand archive search, automatic network discovery or automatic creation of one profile per network.\n"
              "Use /znc or /soju without arguments for service help. Their control text and service-target messages are excluded from input history and local echo; service replies are shown in the server view without persistent logging.\n"
              "Recognized outgoing control copies are suppressed even when replay uses an old nickname. Editing can still show typed text, and the bouncer has its own logging policy.\n"
              "The client connects only to the bouncer. Configure the bouncer's upstream TLS, network authentication and retention separately.\n"
              "See doc/BOUNCERS.txt and the bouncer examples in examples/ for setup and the limits of recorded validation."},
    {"configuration", "[connection] holds server, port, tls, nick, username, realname, channel, autoconnect, auth and credential sources.\n"
              "[bouncer] type=none|znc|soju, username, network, client and automatic_away select a per-network bouncer profile; see /help bouncers.\n"
              "[input] send_mode=direct|review and [output] format=compact|full, queue_notices=true|false, program_messages=true|false, and persistent_logging=true|false are application preferences in the main config file.\n"
              "[input] command_suggestions=common|history|frequent|alphabetical|contextual selects command ordering; common is the default.\n"
              "[input] composer_mode=line|fullscreen selects the minimalist composer/command layout; line is the default and both layouts retain a live tail.\n"
              "[input] use_signals=false disables terminal-generated signals while this client owns the tty. true enables the existing terminal signal mappings. Apply takes effect now; Save persists this primary-owned preference.\n"
              "[hotkeys] contains optional KEY=action bindings, up to 64, with no defaults. It is owned by the primary config; see /help hotkeys.\n"
              "[session] single_session=true|false controls whether more than one background job may exist; false is the default.\n"
              "[session] default_interface=ncurses|idle is a primary-owned application preference. The supported startup choices are described below.\n"
              "Compact output is the default. The main history_lines value controls native retention across all servers.\n"
              "[mentions] word= and [ignore] mask= may repeat.\n"
              "[autocommands] contains enabled, delay_seconds, interval_seconds and repeated command= lines for one server.\n"
              "These command lines are literal plaintext in a private config; /autocommands manages them without exposing secret text in lists or history.\n"
              "Saved extra servers use the adjacent .servers catalog and .d directory.\n"
              "Connect with /connect HOST, then use /join #channel and /saveconf to remember your working configuration.\n"
              "/saveconf saves preferences and healthy server profiles, and remembers the selected server for a later bare /connect.\n"
              "/quickstart provides four connection questions. /panel uses 0 for application preferences and 1..8 for profiles; panel 1 edits the primary.\n"
              "Panel edits remain private until Finish. Accepting an offered GPG destination can prepare its 0700 directory; encryption, configuration writes and connection actions wait for the final choice.\n"
              "To retain changes across restarts, use /save for the selected server, /save input or /save output for the primary profile, or /saveconf for healthy profiles plus the selected default.\n"
              "Saved configurations use <config_path>.commands for automatic personal count reads and writes; the main config does not store counts.\n"
              "This private bounded file contains canonical names and counts only, never arguments or message history. Test mode does not read or write it.\n"
              "[connection] edits staged by /autojoin or /autoauth take effect on the next disconnected /connect, without altering a live connection.\n"
              "Config files are private; writes replace individual files atomically, not the whole workspace as one transaction. No import/export commands exist.\n"
              "Unknown sections or keys and invalid values are errors. Configuration uses literal values, not shell quoting, escapes, variables or ~ expansion.\n"
              "Ordinary values are trimmed at their ends. password= and server_password= preserve everything after = except the line ending; no inline comment or quotes should be added.\n"
              "Save rewrites supported settings and replaces hand-written formatting/comments. Keep notes outside the generated configuration.\n"
              "See /help authentication for secrets, /help tls for CA files, /help logging for disk transcripts and /help sessions for live-worker controls."},
    {"deployment", "Use --background to launch a client session without a terminal or a welcome keypress.\n"
              "Configure and save /autoconnect on for every server to start.\n"
              "--background does not enable autoconnect by itself; --no-connect still suppresses startup connections.\n"
              "The usual warning is printed once before detaching. Success confirms a local background job, not a successful IRC login.\n"
              "Use --sessions and --resume [NUMBER] from a later terminal under the same user account.\n"
              "--sessions includes attached jobs. --detach-session NUMBER non-destructively releases an attached terminal so it can be resumed elsewhere. --close-session NUMBER ends the selected job and every IRC connection.\n"
              "Use the listed job ID, not a PID. Unresponsive workers and legacy workers lacking a requested control can fail. See /help sessions for recovery and upgrade steps.\n"
              "A VPS reboot ends all old jobs, drafts and in-memory history. An OS startup job can launch a fresh client.\n"
              "OpenBSD instructions and an optional rc.d example are in doc/OPENBSD-VPS.txt.\n"
              "Run the client as a normal user with absolute config and credential paths. A locked GPG key cannot prompt at boot.\n"
              "--check-config validates settings, not credential access, DNS, TLS or IRC login.\n"
              "Use --reconnect for temporary network outages; process-crash supervision is a separate operating-system task.\n"
              "Launchers must leave children waitable: ignored SIGCHLD or automatic child reaping makes first background startup, resolver or credential-helper creation fail. A managed re-detach creates no new worker."},
    {"documentation", "Find a task with /help ls; /help commands explains command entry and lists supported commands. Use one topic or command name at a time.\n"
              "/help COMMAND gives syntax, effects and examples. /help /ls explains the local list command; /help ls lists topics.\n"
              "/help all or /manual opens this reference. Command-line --help describes launch options; minimalist --manual output can be redirected to a file.\n"
              "README.md maps setup/docs, MANUAL.txt covers the main interface, and INSTALL covers building. The installed manual page explains shell use.\n"
              "CONTRIBUTING.md explains changes, doc/SOURCE-MAP.txt locates code, and tests/README.md maps test targets. DESIGN.md records ownership and ordering.\n"
              "doc/TROUBLESHOOTING.txt gives symptom-led checks. SECURITY.md states the threat model; doc/BOUNCERS.txt and doc/SESSION-CONTROLS.txt cover operations. Audits apply only to their stated versions/environments."},
    {"history", "Recent displayed records and command history are bounded in memory. Persistent logging is optional and disabled by default.\n"
              "Changing a view replays its retained recent records; /mentions replays retained matching source lines.\n"
              "The application history setting caps records across all servers/views together; edit it through /panel, then 0.\n"
              "Retention is limited to the smaller of that setting and 2048 records, with a separate 512 KiB hard byte cap.\n"
              "/catchup in a channel or query starts at your most recent locally sent composition. It tries an eligible private log, then retained memory; /help catchup explains the two readers.\n"
              "Durable catch-up is per server and conversation. Persistent logging must already be enabled when you send the anchor; it is a private plaintext transcript capped at 16 MiB per saved profile, not encrypted storage.\n"
              "Command recall retains at most 64 entries and 64 KiB, evicting the oldest. /clear clears conversation history, not command recall.\n"
              "Recent history suggestions use these complete lines. Delete removes a selected history row.\n"
              "Most-used suggestions instead use at most 96 canonical command names and bounded counts in <config_path>.commands.\n"
              "For saved configurations, accepted commands and frequency resets update that private file automatically. Arguments, targets and messages are not persisted in it.\n"
              "Unsaved profiles keep counts in memory until the first successful Save. Missing or unusable count files do not prevent startup.\n"
              "Help and multi-line command responses use a separate reader backed by a 512 KiB bounded buffer. They do not become chat history.\n"
              "Accepted commands can be recalled in the command editor. Authentication changes, keyed joins and sensitive service messages are omitted.\n"
              "Compose Up/Down moves the caret; it does not recall sent chat. Only accepted command lines are recalled in Command.\n"
              "Same-interface detach/resume retains drafts and recall. Switching interfaces, where supported, discards drafts, recall and setup forms; exit or reboot also ends retained in-memory chat and catch-up anchors.\n"
              "Saved frequency counts contain names only. Old plaintext logs survive exit but are not automatically imported into a new session or into the editor.\n"
              "/clear removes retained memory for a view, not its old disk transcript. Bouncer replay is separately supplied by the bouncer."},
    {"hotkeys", "Custom key bindings are optional and empty by default. Existing navigation and editor controls remain available.\n"
              "Use /bindings to inspect the list, /bindings keys for key names and /bindings actions for the local function list.\n"
              "Assign with /bind KEY ACTION; replace by binding the same key again. Remove with /unbind KEY or /unbind all.\n"
              "Changes apply to all servers immediately; /save input saves them in the primary configuration. Extra server profiles do not override that table.\n"
              "The config form is [hotkeys] followed by one KEY=action per line, up to 64. Duplicate keys, including terminal aliases, are errors.\n"
              "Custom actions run only in the main chat and command prompt. They do not override passwords, setup, paste, readers or confirmation screens.\n"
              "In the minimalist interface, read-only shortcut readers preserve the draft/caret. A shortcut that changes its target or editor refuses a nonempty draft; finish it or deliberately clear it first.\n"
              "The allowlist has no send action, arbitrary commands, shell code or text macros. Background and quit have their existing program effects.\n"
              "F1..F12, Ctrl-A..Z, Alt-a..z/0..9 and named navigation keys are available; ordinary printable characters remain text.\n"
              "Ctrl-H/Backspace, Ctrl-I/Tab, Ctrl-J/Ctrl-M/Enter and Ctrl-[/Escape share terminal bytes and cannot have separate bindings.\n"
              "Terminal signals and terminal/multiplexer shortcuts take precedence. A Ctrl-C or Ctrl-Z binding works only if that byte reaches the application; no terminal key is reassigned.\n"
              "Recognized bracketed paste cannot run bindings. Unframed paste is indistinguishable from typing. See /help keys for terminal timing and paste limits."},
    {"keys", "Log: / opens Command; ? explains the selected conversation/watch log. Other printable keys open Compose with that character retained.\n"
              "Custom bindings may override these controls in main chat/Command; none are assigned by default. See /help hotkeys.\n"
              "Live log: Up scrolls a row older, Page Up a page, Home the oldest retained page. Down/Page Down/End stay live; Left/Right/Delete/Backspace do not open Compose.\n"
              "Insert or Enter opens empty Compose. Arrows, Home/End, Delete and Backspace edit text. Tab/Shift-Tab completes nicknames in Compose and command names in Command.\n"
              "Line composer mode keeps one input row below the live tail; fullscreen keeps a multi-line editor with a smaller live-tail pane.\n"
              "Command Down selects suggestions. Up/Down moves through them; Up from the first returns to the input.\n"
              "Suggestion Enter inserts without running. Read/edit it, then press Enter separately after display to execute.\n"
              "Command Up recalls matching accepted history; Down returns toward the original input. Compose Up/Down moves the caret, even when held; Enter submits.\n"
              "Delete removes a selected Recent history row or resets a selected Most-used count. Catalog commands remain available.\n"
              "? on a selected command explains it and links to command-screen help. In all text editors, ? is ordinary text.\n"
              "Review uses a temporary alternate screen; its draft/prompt never enter chat history. Fresh Enter confirms; Escape or typing restores the editor/draft/caret. Up/Down, Page Up/Down and Home/End browse it.\n"
              "Help/response Up/Down scrolls; Page Up/Down changes page; Home/End reaches the beginning/end.\n"
              "Roster Enter returns to the log; Help and other response readers use Enter to scroll one line.\n"
              "In the reader, / opens Command with normal completion/history. Escape cancels that prompt back to the reader.\n"
              "In a summary reader, r refreshes the snapshot; ? explains the response and its reader.\n"
              "In context help, Enter follows the highlighted About screen link. Escape returns to item help, then to the exact originating screen.\n"
              "Setup Tab selects the question heading; ? explains its option/screen. Tab/Enter returns to answer editing.\n"
              "Executing a command replaces the previous response. Short acknowledgments return to the log; short errors keep the command editable.\n"
              "Escape cancels a Compose/Command editor to the log. From Review, Escape returns to that editor without discarding the draft. Escape on the log backgrounds the client.\n"
              "Bare Escape waits about 500 ms; a sequence in progress allows 1000 ms between continuation bytes. Networking continues.\n"
              "Complete key sequences are handled immediately. An opening paste marker delayed after its first Escape past that grace can background the client.\n"
              "Once paste capture starts, its closing marker remains recognized across arbitrarily delayed, split reads.\n"
              "The inherited terminal interrupt, quit, and suspend key assignments remain unchanged; Use signals = no disables their signal effects while the client is active.\n"
              "[input] use_signals=false is the default. Ctrl-C/Ctrl-Z cancel unfinished input; true enables terminal interrupt/suspend signals without remapping keys.\n"
              "Enabled terminal signals terminate or suspend after restoring the terminal; fg resumes a suspended job.\n"
              "Ctrl-D is inert unless bound in main chat/Command. /quit exits; /background releases the terminal while networking continues.\n"
              "Routine screens omit recurring key hints; this topic remains the complete keyboard reference.\n"
              "Recognized bracketed paste stays literal. Keys wait for a visible editor, fresh input and a pause. Tiny/failed frames cannot approve it; resize and release held keys.\n"
              "Watch-log paste opens Compose with literal slashes. Paste inside Command stays there; CR/LF/Tab become spaces. Review it, then press a fresh Enter.\n"
              "Unframed paste is indistinguishable from typing; use bracketed-paste support."},
    {"limits", "IRC limits are byte limits, not counts of displayed glyphs. UTF-8 characters can occupy more than one byte.\n"
              "The client computes a message budget including target, command and relay overhead; it never splits a UTF-8 scalar.\n"
              "At one-message capacity, further text is blocked until Enter explicitly acknowledges continued editing; that Enter sends nothing.\n"
              "A multi-message composition still requires separate consent before queueing, even in direct mode. Chat is paced at one line per 1.2 seconds.\n"
              "A send plan has at most 64 messages and 16384 bytes of composed text. More text must be shortened or sent deliberately in separate submissions.\n"
              "There are at most 8 server slots and 32 views per server, including reserved views."},
    {"logging", "Persistent logging is optional and off by default. /set persistent_logging on enables it; /save output saves that application preference.\n"
              "Each saved server profile appends to CONFIG_PATH.log. The final parent directory must be owned with mode 0700; the file must be owned, regular, single-link and exactly 0600. Symlinks and parent traversal are refused.\n"
              "Logs are plaintext, not encrypted backups. Each file stops at 16 MiB. An unsafe path, write error or busy file stops logging with a visible error while live chat continues.\n"
              "Archive a full log outside the active path, then toggle logging off/on to resume. Rotation or rewriting can invalidate the previous catch-up anchor; send a new eligible message before relying on a later absence's disk catch-up.\n"
              "Known credential/service traffic and unsent drafts are excluded, including replayed authentication under old nicknames and qualified service targets. Ordinary chat, unknown service formats and custom secret text may still be recorded. Server and bouncer retention are separate policies.\n"
              "/catchup can use a healthy log only when its exact locally sent anchor was recorded in that running session. It never asks the bouncer to fetch an archive and does not import old logs after a process restart.\n"
              "/clear clears memory only. Existing log files survive quit, --close-session and configuration saves. Turning logging off stops new records without deleting old ones.\n"
              "Regular disk I/O can delay the process on slow storage. See /help catchup, /help history and SECURITY.md for the limits."},
    {"messages", "Use /join #channel to join and select a channel, or /query NICK to open a private conversation.\n"
              "Select a retained channel with /channel. Use /msg TARGET TEXT for an explicit destination or /me TEXT for an action.\n"
              "/query opens a conversation without sending; /pm is an alias. /ql keeps queries separate from channels.\n"
              "Direct send is the default; /set send_mode review adds a review stage. /set composer_mode line|fullscreen changes presentation.\n"
              "Reaching one-message capacity requires a continued-editing acknowledgement; splitting still requires separate send consent.\n"
              "Joining and authentication must finish before messages can be queued. /cancel cancels queued chat, not already-sent text."},
    {"reconnect", "The shell option --reconnect enables retries for this process; it does not save a setting or initiate a connection.\n"
              "Without this option, a failed connection still requires /connect. /autoconnect controls startup only.\n"
              "Temporary DNS, refused/lost connections and server timeouts retry after 5, 10, 20, 40, 80, 160 and then 300 seconds.\n"
              "A registered connection lasting at least 60 seconds resets that backoff. Polling continues without blocking sleeps.\n"
              "Certificate verification, authentication, malformed-protocol and configuration errors remain disconnected for correction.\n"
              "A server ERROR before registration is held; one after registration can trigger a reconnect.\n"
              "Explicit /disconnect cancels a pending retry. A later /connect starts a fresh attempt.\n"
              "Retries use the current connection settings; staged edits still need an explicit /connect.\n"
              "Pending chat messages are discarded on disconnect and are never automatically resent.\n"
              "Configured authentication and enabled startup commands run again on a new connection. Direct profiles replay autojoin after registration/authentication; bouncers restore their own memberships.\n"
              "Offline test mode never reconnects to the network."},
    {"security", "Remote text is sanitized before terminal output. Inputs are parsed as local IRC commands, never shell commands.\n"
              "There is no /raw, shell escape or automatic file acceptance. Recognized bracketed paste never submits a command.\n"
              "TLS verifies the server identity. SASL, server PASS and recognized private authentication commands require verified TLS. Keep the system TLS library patched as well as the client; see SECURITY.md.\n"
              "A custom CA bundle must be a nonempty regular PEM file of at most 16 MiB; it never prompts for a password.\n"
              "Configured passwords are redacted from help/settings. Recognized credential commands are excluded from recall; secrets typed as ordinary text may still be retained.\n"
              "No disk conversation transcript is enabled by default. Configuration changes require an explicit save.\n"
              "A process under the same account, the IRC server/bouncer, and a compromised terminal or operating system remain outside these protections. TLS is not end-to-end chat encryption.\n"
              "Optional logs and inline configuration secrets are plaintext. Known sensitive commands are filtered, but arbitrary secrets in ordinary chat cannot be recognized reliably.\n"
              "See /help tls, /help authentication, /help logging and SECURITY.md for exact boundaries and historical review scope."},
    {"sessions", "A job is one running client worker with all of its servers, transfers, drafts and retained history. A server number inside the application is not a shell job ID.\n"
              "From a shell, run the application with --sessions. Inspect its current attached/detached status and use that listed job ID, not a process ID.\n"
              "If an old SSH terminal still owns the job, run --detach-session NUMBER, then --resume NUMBER. Detach releases the controller while keeping IRC and state alive.\n"
              "Bare --resume selects only a sole verified detached job. Multiple jobs require a number; an attached job is busy until detached. Job numbers may be reused after exit.\n"
              "Use --close-session NUMBER only when the whole job should end. It closes IRC and transfers and loses unsaved drafts and in-memory history; saved configs and existing logs remain.\n"
              "Close reports success only after verified cleanup. If it times out, recheck --sessions: the request may finish later. Failure does not remove a live endpoint or signal a guessed PID.\n"
              "Only a managed job can survive terminal loss. An ordinary foreground process that has never backgrounded is not made resumable by SSH disconnection. Start with --background or use /background first.\n"
              "A job belongs to the same Unix account and host, with one attached terminal at a time. Another program under that account is within the local trust boundary.\n"
              "Installing an executable does not replace a running worker's code. Save desired configuration, quit or close the old worker, check the installed --version, then start a fresh worker.\n"
              "A legacy worker may lack newer controls or interface switching. Keep its compatible executable until it is stopped if required for attachment.\n"
              "The client does not change SSH idle-timeout policy. See /help background, /help deployment and doc/SESSION-CONTROLS.txt."},
    {"startup", "Run the program, dismiss the welcome, then type /connect irc.example.org and /join #channel.\n"
              "No server profile, alias or number needs to be created first. /server HOST is an alias for direct connection.\n"
              "When no nickname is supplied, your valid system login name is used, with user as the fallback. No nickname prompt is required.\n"
              "Use /saveconf to save your configuration and selected default. Next launch, /connect reuses it.\n"
              "Saving does not enable autoconnect; that remains an explicit choice. /quickstart and /panel are optional guides.\n"
              "Shell options also work: --server HOST, --channel '#CHANNEL' and --nick NICK. Quote #CHANNEL for the shell.\n"
              "At normal startup, --nick, --port, --channel, --tls, --plain and --ca-file override the remembered default.\n"
              "An explicit --server selects the first connection instead. --init-config and --check-config use the main config file.\n"
              "Changing --server to a different host clears the old host's credentials, CA override, bouncer identity, channels and startup commands.\n"
              "If the remembered default is unavailable, specify --server or repair it before applying connection overrides.\n"
              "Autoconnect, autojoin and autoauth belong to each saved server profile. /help autoconnect explains the toggle.\n"
              "--no-connect suppresses startup connections; --test-mode simulates activity without network or config writes.\n"
              "--sessions lists attached and detached jobs. --resume [NUMBER] reattaches; /help background explains selection.\n"
              "--close-session NUMBER closes the selected whole job and all its IRC connections after verified completion. NUMBER is a job ID, not a PID.\n"
              "After terminal loss, use --sessions, then --detach-session NUMBER and --resume NUMBER. A running worker keeps its loaded code until restarted; see /help sessions.\n"
              "-h/--help and -v/--version work without a terminal."},
    {"tls", "Verified TLS is the default. The requested server hostname or numeric address and its certificate chain must verify; there is no verification bypass or silent plaintext fallback.\n"
              "A port number alone does not disable TLS. Explicit --plain or /connect -plain selects unencrypted IRC and cannot use configured account/server credentials.\n"
              "Use the provider's exact TLS hostname and port. A failure may be a wrong endpoint, expired/untrusted certificate or local trust configuration; inspect the concrete connection error before changing settings.\n"
              "By default the TLS library's trust store is used. --ca-file FILE or connection ca_file= selects a custom PEM CA bundle; it does not pin a leaf certificate.\n"
              "A custom bundle must be a nonempty regular file of at most 16 MiB. An ordinary symlink to a regular CA file is allowed; secret/config paths have stricter no-symlink rules.\n"
              "The custom file is opened once, bounded and checked for size changes while read. FIFOs, devices and other nonregular inputs are rejected, and password-protected PEM cannot open an interactive prompt.\n"
              "Bounded file size does not bound regular-file or network-filesystem I/O latency. Use a trustworthy readable CA bundle on suitable storage.\n"
              "--check-config does not perform a handshake or prove that a CA or credential source will work. --test-mode never contacts a server.\n"
              "TLS protects this connection only. With a bouncer, the upstream connection and retained plaintext history are controlled by the bouncer separately."},
    {"troubleshooting", "Resume says busy after SSH loss: run --sessions, verify the job, then --detach-session NUMBER followed by --resume NUMBER. See /help sessions before using destructive --close-session.\n"
              "Up does not recall text: Compose edits message text; Command recalls accepted commands. Open Command with / from the log. Sent chat and sensitive commands are not recall history.\n"
              "A wrapped message seems clipped: the input view reserves rows for its editor and shows the newest visual rows above it. Leave the editor or reselect the channel to inspect retained text; visible rows alone do not prove remote delivery.\n"
              "Changing channels showing the complete text indicates a viewport difference. If a rendering fault persists, record interface, composer mode, terminal dimensions, locale and sanitized steps without sharing passwords or private transcripts.\n"
              "No channels after connect: inspect the server view and finish registration/authentication. Direct profiles use /autojoin list; bouncers restore memberships and use /join or /part.\n"
              "Authentication fails: check the account, the one selected source, private permissions, TLS endpoint and GPG agent availability. /autoauth status shows configured paths without reading secrets.\n"
              "Catch-up unavailable: the anchor may have rolled out, logging may have started later/stopped, or the log changed. See /help catchup and /help logging; saved logs do not recreate a dead worker.\n"
              "An applied setting seems ignored: connection changes need a disconnected /connect, while saving and startup autoconnect are separate choices. Interface and terminal-signal launch preferences require a fresh launch.\n"
              "Background/helper startup fails with child ownership: correct the launcher's SIGCHLD/automatic-reaping policy so the client can wait for its children.\n"
              "Use --check-config for configuration diagnostics and doc/TROUBLESHOOTING.txt for ordered checks. Offline --test-mode tests the interface, not DNS, TLS, credentials or a real bouncer."}
};

typedef struct Word { const char *text, *rest; size_t size; } Word;

static unsigned char lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

static Word word(const char *text)
{
    Word w;
    while (*text == ' ') ++text;
    w.text = text;
    while (*text && *text != ' ') ++text;
    w.size = (size_t)(text - w.text);
    while (*text == ' ') ++text;
    w.rest = text;
    return w;
}

static int equal(Word w, const char *text)
{
    if (w.size != strlen(text)) return 0;
    for (size_t i = 0; i < w.size; ++i)
        if (lower((unsigned char)w.text[i]) != (unsigned char)text[i]) return 0;
    return 1;
}

static int copy_word(char *out, size_t cap, Word w)
{
    if (!w.size || w.size >= cap) return 0;
    memcpy(out, w.text, w.size); out[w.size] = 0;
    return 1;
}

static size_t count_words(const char *text)
{
    size_t count = 0;
    Word w = word(text);
    while (w.size) { ++count; w = word(w.rest); }
    return count;
}

static const Command *find_command(Word name)
{
    const char *alias = commands_alias_name(name.text, name.size);
    if (alias) { name.text = alias; name.size = strlen(alias); }
    for (size_t i = 0; i < sizeof catalog / sizeof catalog[0]; ++i)
        if (equal(name, catalog[i].spec.name)) return &catalog[i];
    return NULL;
}

size_t idle_command_count(void) { return sizeof catalog / sizeof catalog[0]; }
const IdleCommandSpec *idle_command_spec(size_t i)
{
    return i < idle_command_count() ? &catalog[i].spec : NULL;
}

static void emit(IdleCommands *c, const char *format, ...)
{
    char raw[2048], safe[4096];
    va_list ap;
    va_start(ap, format); (void)vsnprintf(raw, sizeof raw, format, ap); va_end(ap);
    irc_sanitize(safe, sizeof safe, raw);
    if (c && c->print) c->print(c->context, safe);
}

static const char *display_text(const IdleCommands *c, const char *english)
{
    return c && c->translate ? c->translate(english) : english;
}

/* Use the command reader's optional lookup, including English-only fixtures. */
static const char *display_config_error(const IdleCommands *c, const char *raw,
                                        char *out, size_t cap)
{
    const char *shown = irc_config_number_diagnostic_lookup(raw, out, cap, c ? c->translate : NULL);
    return shown != raw ? shown : display_text(c, raw);
}

/* Translate templates, not literal values. Use the same optional callback
 * as the command help, including the explicitly English manual/fixtures. */
static void emit_fields(IdleCommands *c, const char *key,
                        const IrcDisplayField *fields, size_t count)
{
    char raw[2048];
    if (!irc_display_render_lookup(raw, sizeof raw, key, fields, count,
                                   c ? c->translate : NULL) &&
        !irc_display_render_lookup(raw, sizeof raw, key, fields, count, NULL)) return;
    emit(c, "%s", raw);
}

/* Generated help has a small, fixed set of program names and default values.
 * Format its trusted English source first, then look up the complete sentence.
 * A translated string is always a %s argument, never a printf format. Keep
 * this helper exclusive to help: arbitrary server text, settings and user
 * data must not accidentally match a catalog key and change on display. */
static void help_emit(IdleCommands *c, const char *format, ...)
{
    char english[2048];
    va_list ap;
    va_start(ap, format);
    int length = vsnprintf(english, sizeof english, format, ap);
    va_end(ap);
    if (length < 0) return;
    emit(c, "%s", (size_t)length < sizeof english ?
         display_text(c, english) : english);
}

/* Translate only complete literal lines of human-readable help. The command
 * catalog's syntax, examples and dispatch remain unchanged. Very long lines
 * are emitted in English instead of truncating an incomplete UTF-8 sequence. */
static void paragraphs(IdleCommands *c, const char *text)
{
    while (*text) {
        const char *end = strchr(text, '\n');
        size_t n = end ? (size_t)(end - text) : strlen(text);
        char line[2048];
        if (n < sizeof line) {
            memcpy(line, text, n);
            line[n] = 0;
            emit(c, "%s", display_text(c, line));
        } else emit(c, "%.*s", (int)n, text);
        if (!end) break;
        text = end + 1;
    }
}

static const char *program_name(const IdleCommands *c)
{
    return c && c->program && *c->program ? c->program : "onekeyirc";
}

static void launch_interface_help(IdleCommands *c)
{
    if (!strcmp(program_name(c), "vipirc")) {
        help_emit(c, "Default interface: ncurses. /set default_interface idle|ncurses chooses the next fresh launch; /save input saves the choice in the main config.");
        help_emit(c, "Explicit -i/--idle or -N/--ncurses overrides that launch without saving the override. Lowercase -n still selects the nickname.");
    } else {
        help_emit(c, "%s always uses idle. Its application panel omits interface selection; /set default_interface accepts only idle.", program_name(c));
        help_emit(c, "The shared config parser preserves either recognized saved value. A stored ncurses value does not change this standalone client's interface.");
    }
}

static void session_shell_examples(IdleCommands *c)
{
    const char *program = program_name(c);
    help_emit(c, "From the shell: %s --sessions; %s --detach-session NUMBER; %s --resume NUMBER; %s --close-session NUMBER.", program, program, program, program);
    if (!strcmp(program, "vipirc"))
        help_emit(c, "Bare %s --resume retains the worker's current interface. %s -i --resume NUMBER selects minimalist; %s -N --resume NUMBER selects ncurses. Changing interfaces keeps IRC/history/transfers but discards drafts, pending send reviews, command recall and unapplied forms.", program, program, program);
}

static void hotkey_frontend_help(IdleCommands *c)
{
    if (!strcmp(program_name(c), "vipirc"))
        help_emit(c, "In full ncurses, App -> Key bindings or /bindings opens an editable table with Apply, Save and Escape/Cancel; -i uses the commands.");
    else help_emit(c, "This client edits bindings through commands; /bindings lists the current table.");
}

/* Under a UTF-8 LC_CTYPE, count terminal cells rather than UTF-8 bytes.
 * A malformed or unsupported byte is counted as one fallback cell. This is
 * only a decorative underline; malformed bytes never affect commands. */
static size_t heading_cells(const char *title, size_t limit)
{
    size_t cells = 0;
    mbstate_t state = {0};
    for (const char *p = title; *p && cells < limit;) {
        wchar_t wc;
        size_t n = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        if (n == (size_t)-1 || n == (size_t)-2 || n == 0) {
            memset(&state, 0, sizeof state);
            ++cells; ++p;
            continue;
        }
        int width = wcwidth(wc);
        if (width > 0) {
            size_t span = (size_t)width;
            cells += span > limit - cells ? limit - cells : span;
        }
        p += n;
    }
    return cells;
}

static void heading(IdleCommands *c, const char *title)
{
    char rule[73];
    size_t n = heading_cells(title, sizeof rule - 1);
    memset(rule, '-', n); rule[n] = 0;
    emit(c, "%s", title);
    emit(c, "%s", rule);
}

static int usage(IdleCommands *c, const Command *command)
{
    emit(c, "%s %s", display_text(c, "Usage:"), command->spec.usage);
    emit(c, "%s /help %s", display_text(c, "Detailed help:"), command->spec.name);
    return IDLE_COMMAND_ERROR;
}

static void command_help(IdleCommands *c, const Command *command)
{
    char title[2048];
    (void)snprintf(title, sizeof title, "/%s - %s", command->spec.name, display_text(c, command->spec.description));
    heading(c, title);
    emit(c, "%s %s", display_text(c, "Usage:"), command->spec.usage);
    emit(c, "");
    paragraphs(c, command->detail);
    if (!strcmp(command->spec.name, "background"))
        session_shell_examples(c);
    if (!strcmp(command->spec.name, "set")) launch_interface_help(c);
    if (!strcmp(command->spec.name, "bindings")) hotkey_frontend_help(c);
    emit(c, ""); emit(c, "%s", display_text(c, "Examples:")); paragraphs(c, command->examples);
}

static void topic_help(IdleCommands *c, const Topic *topic)
{
    const char *program = program_name(c);
    int full = !strcmp(program, "vipirc");
    heading(c, topic->name);
    if (!strcmp(topic->name, "hotkeys")) hotkey_frontend_help(c);
    if (!strcmp(topic->name, "configuration")) {
        help_emit(c, "Default config file: $XDG_CONFIG_HOME/%s/config.ini; XDG_CONFIG_HOME must be absolute.", program);
        help_emit(c, "When XDG_CONFIG_HOME is unset or empty: $HOME/.config/%s/config.ini. --config FILE selects another file.", program);
        if (full) {
            help_emit(c, "Minimalist mode uses the usual application configuration and saved server catalog.");
            help_emit(c, "[interface] history_lines= sets the retained-chat limit; other interface preferences remain saved.");
        } else {
            help_emit(c, "If the new default is absent, an existing 1keyirc/config.ini under the same configuration base is used in place.");
            help_emit(c, "Existing settings, server catalogs and credential paths are not moved. The onekeyirc default wins when both exist.");
            help_emit(c, "An unsafe or unreadable default is reported instead of silently choosing another file. --config FILE overrides this selection.");
            help_emit(c, "[history] lines= sets the retained-chat limit. Configuration does not contain [interface] options.");
        }
    }
    paragraphs(c, topic->text);
    if (!strcmp(topic->name, "configuration")) launch_interface_help(c);
    if (!strcmp(topic->name, "history")) help_emit(c, "The default application history limit is %d records.", full ? 500 : 100);
    if (!strcmp(topic->name, "startup")) {
        launch_interface_help(c);
        session_shell_examples(c);
        help_emit(c, "%s%s --manual prints this complete manual.", program, full ? " -i" : "");
    }
}

static void command_list(IdleCommands *c)
{
    heading(c, display_text(c, "Commands (use /help COMMAND for syntax and examples)"));
    for (size_t i = 0; i < idle_command_count(); ++i) {
        emit(c, "");
        emit(c, "  /%-12s %s", catalog[i].spec.name, display_text(c, catalog[i].spec.description));
    }
}

static void command_system(IdleCommands *c)
{
    heading(c, display_text(c, "The command system"));
    paragraphs(c,
                  "Press / on the log to open Command; the opening slash is already inserted. Enter with input focus runs the edited command; Escape cancels it.\n"
                  "Syntax: /COMMAND ARGUMENTS. Names are case-insensitive; use the shown option words. UPPERCASE words are placeholders, [items] optional, and | means choose an alternative.\n"
                  "Enter one command at a time. Spaces separate arguments; there is no shell quoting, expansion or chaining. Shell launch options belong outside this editor.\n"
                  "TEXT and credential PATH use the literal remainder, including spaces. To paste a complete /command, remove the prefilled slash first; pasted newlines become spaces, not separate commands.\n"
                  "Home/End/Delete/Backspace edit. Tab/Shift-Tab completes command names; Up from input recalls accepted history.\n"
                  "Down selects suggestions; Enter inserts the selected command for editing. A separate Enter after display executes it.\n"
                  "Choose Common, Recent history, Most used with this configuration, Alphabetical or Contextual in application Settings.\n"
                  "Common is a curated useful order. Contextual remembers the actual screen that opened command entry. Other prefix matches remain available.\n"
                  "? on a selected suggestion opens item and screen help. In the input it remains ordinary text.\n"
                  "Delete removes a selected recent entry or resets a personal frequency count; it never removes a catalog command.\n"
                  "Only accepted safe commands enter bounded in-memory history. Password/service commands and keyed joins are excluded.\n"
                  "A command with unexpected arguments fails without its requested action. Help shows syntax, effects and examples.\n"
                  "Message commands create a send plan. Send mode and mandatory multi-message confirmation apply before queueing.\n"
                  "Most commands act on the selected server. /server NUMBER selects another; /server alone selects its MOTD view.\n"
                  "Use /connect HOST, /join #channel, /query NICK and /saveconf for the normal connection and chat workflow.\n"
                  "Save accepted settings to keep them after restart. Saved profiles update a private .commands file automatically for usage counts only; it contains no arguments or chat.\n"
                  "/summary reads all current client state locally; /list [MASK] requests network channels, while /ls and /cl list local views.\n"
                  "/help ls lists all help topics.");
    command_list(c);
}

static void help_index(IdleCommands *c)
{
    heading(c, display_text(c, "Help topics (use /help TOPIC)"));
    emit(c, "  commands       %s", display_text(c, "Command entry, syntax, completion, history and complete inventory"));
    emit(c, "  language       %s", display_text(c, "Language selection and UTF-8 display"));
    for (size_t i = 0; i < sizeof topics / sizeof topics[0]; ++i) {
        emit(c, "");
        emit(c, "  %s", topics[i].name);
    }
    emit(c, "");
    heading(c, display_text(c, "Command topics (use /help COMMAND; /help /ls explains local lists)"));
    for (size_t i = 0; i < idle_command_count(); ++i) {
        emit(c, "");
        emit(c, "  %-12s %s", catalog[i].spec.name, display_text(c, catalog[i].spec.description));
    }
}

static void overview(IdleCommands *c)
{
    emit(c, "%s %s", program_name(c), display_text(c, "command help"));
    paragraphs(c,
        "Read help topics: /help ls.\n"
        "Command list: /help commands. Details: /help COMMAND.\n"
        "First connection: /connect irc.example.org, then /join #channel.\n"
        "Private chat: /query NICK or /msg NICK TEXT.\n"
        "Guided setup: /quickstart or /panel.\n"
        "Save settings: /saveconf. Saving does not enable autoconnect.\n"
        "Switch views: /servers, /channels, /ql.\n"
        "Startup options: /help autoconnect, /help autojoin, /help autoauth.\n"
        "Recover SSH sessions: /help sessions.\n"
        "Bouncer instructions: /help bouncers.\n"
        "Settings and hotkeys: /help configuration, /help hotkeys.\n"
        "Problems and TLS security: /help troubleshooting, /help security, /help tls.\n"
        "History and logs: /help history, /help logging.\n"
        "Choose language: /help language.\n"
        "Full reference: /help all or /manual.\n"
        "Escape closes the reader; / opens Command.");
}

static void language_help(IdleCommands *c)
{
    heading(c, display_text(c, "Language selection and UTF-8 display"));
    paragraphs(c,
        "Choose the display language before starting the client.\n"
        "Example: VIPIRC_LANG=id onekeyirc\n"
        "Example: VIPIRC_LANG=tr vipirc -i\n"
        "Available codes: id ms tr es pt fr de ru it ar.\n"
        "Use a UTF-8 terminal, font and installed UTF-8 locale.\n"
        "IRC commands, help topics and configuration keys stay in English.\n"
        "Arabic text requires terminal shaping and bidirectional support.\n"
        "Restart the program to change the language of an existing worker.");
}

static void manual_to(IdleCommands *c)
{
    help_emit(c, "%s - command-oriented terminal IRC", program_name(c));
    help_emit(c, "Not vegetarian? www.bloodguiltcurse.net will change your mind");
    overview(c);
    for (size_t i = 0; i < sizeof topics / sizeof topics[0]; ++i) {
        help_emit(c, ""); topic_help(c, &topics[i]);
    }
    help_emit(c, ""); command_system(c);
    for (size_t i = 0; i < idle_command_count(); ++i) { help_emit(c, ""); command_help(c, &catalog[i]); }
}

static void file_print(void *context, const char *line)
{
    (void)fprintf(context, "%s\n", line);
}

void idle_manual_translated(FILE *out, const char *program,
                            const char *(*translate)(const char *))
{
    IdleCommands c = {NULL, program, file_print, out, 0, 1, NULL, translate};
    if (out) manual_to(&c);
}

/* Preserve deterministic English output for source/manual generation tools. */
void idle_manual(FILE *out, const char *program)
{
    idle_manual_translated(out, program, NULL);
}

static int help(IdleCommands *c, const char *rest, const Command *command)
{
    Word requested = word(rest);
    if (!requested.size) { overview(c); return IDLE_COMMAND_DONE; }
    if (*requested.rest) return usage(c, command);
    int explicit_command = *requested.text == '/';
    if (explicit_command) { ++requested.text; --requested.size; }
    if (!explicit_command && equal(requested, "ls")) { help_index(c); return IDLE_COMMAND_DONE; }
    if (equal(requested, "commands")) { command_system(c); return IDLE_COMMAND_DONE; }
    if (equal(requested, "language")) { language_help(c); return IDLE_COMMAND_DONE; }
    if (equal(requested, "all")) { manual_to(c); return IDLE_COMMAND_DONE; }
    if (equal(requested, "send_mode") || equal(requested, "composer_mode") || equal(requested, "output_format") || equal(requested, "output") ||
        equal(requested, "queue_notices") || equal(requested, "program_messages") || equal(requested, "persistent_logging") ||
        equal(requested, "single_session") || equal(requested, "default_interface") || equal(requested, "command_suggestions")) requested = word("set");
    const Command *found = find_command(requested);
    if (found) { command_help(c, found); return IDLE_COMMAND_DONE; }
    for (size_t i = 0; i < sizeof topics / sizeof topics[0]; ++i)
        if (equal(requested, topics[i].name)) { topic_help(c, &topics[i]); return IDLE_COMMAND_DONE; }
    emit(c, "%s", display_text(c, "No such help topic. Use /help ls or /help commands."));
    return IDLE_COMMAND_ERROR;
}

static App *selected(IdleCommands *c)
{
    char error[512];
    if (!c || !c->client || c->client->current >= CLIENT_MAX_SERVERS) return NULL;
    App *a = client_get_app(c->client, c->client->current, error, sizeof error);
    if (!a) emit(c, "%s", error);
    return a;
}

static int number(Word w, size_t maximum, size_t *out)
{
    if (!w.size) return 0;
    size_t value = 0;
    for (size_t i = 0; i < w.size; ++i) {
        unsigned char ch = (unsigned char)w.text[i];
        if (ch < '0' || ch > '9' || (size_t)(ch - '0') > maximum ||
            value > (maximum - (size_t)(ch - '0')) / 10u) return 0;
        value = value * 10u + (size_t)(ch - '0');
        if (value > maximum) return 0;
    }
    *out = value; return 1;
}

static size_t server_label(const IdleCommands *c, size_t index)
{
    return index + (c->server_base ? 1u : 0u);
}

static int server_number(IdleCommands *c, Word w, size_t *index)
{
    size_t value, base = c->server_base ? 1u : 0u;
    if (*w.rest || !number(w, CLIENT_MAX_SERVERS - 1u + base, &value) || value < base ||
        !c->client->slots[value - base].used) {
        emit(c, "%s", display_text(c, base ?
            "No such server number. Servers start at 1; /servers lists them. Panel 0 contains application preferences." :
            "No such server number. Use /servers to list available slots."));
        return 0;
    }
    *index = value - base;
    return 1;
}

/* Format counts separately so conversation rows stay within the four-field
 * display renderer contract. Never translate buffer names or command tokens. */
static void conversation_counts(IdleCommands *c, const Buffer *b, char *out, size_t cap)
{
    char unread[32], mentions[32];
    (void)snprintf(unread, sizeof unread, "%d", b->unread);
    (void)snprintf(mentions, sizeof mentions, "%d", b->unread_mentions);
    const IrcDisplayField fields[] = {{"unread", unread}, {"mentions", mentions}};
    if (!irc_display_render_lookup(out, cap, "{unread} unread, {mentions} mentions", fields, 2,
                                   c->translate))
        (void)irc_display_render_lookup(out, cap, "{unread} unread, {mentions} mentions", fields, 2, NULL);
}

static void list_channels(IdleCommands *c, App *a)
{
    char number_text[32], counts[512], status_counts[1024];
    (void)snprintf(number_text, sizeof number_text, "%zu", server_label(c, c->client->current));
    emit_fields(c, "Channels on server {number}:", (const IrcDisplayField[]){{"number", number_text}}, 1);
    conversation_counts(c, &a->buffers[0], counts, sizeof counts);
    emit_fields(c, "  0: server{selected} | {counts}", (const IrcDisplayField[]){{"selected", a->current == 0 ? display_text(c, " (selected)") : ""}, {"counts", counts}}, 2);
    size_t number = 0;
    for (size_t i = 3; i < a->buffer_count; ++i) {
        const Buffer *b = &a->buffers[i];
        if (b->kind != B_CHANNEL) continue;
        (void)snprintf(number_text, sizeof number_text, "%zu", ++number);
        conversation_counts(c, b, counts, sizeof counts);
        (void)snprintf(status_counts, sizeof status_counts, "%s | %s", display_text(c, b->joined ? "joined" : "parted"), counts);
        emit_fields(c, "  {number}: {name}{selected} | {counts}", (const IrcDisplayField[]){{"number", number_text}, {"name", b->name}, {"selected", a->current == i ? display_text(c, " (selected)") : ""}, {"counts", status_counts}}, 4);
    }
    if (!number) emit(c, "%s", display_text(c, "No channel views. Use /join #channel after connecting."));
}

/* Compose bounded summary fields, keeping the shared renderer's four-field
 * limit and leaving command option tokens, addresses and topics literal. */
static void render_reader_fields(IdleCommands *c, char *out, size_t cap,
                                 const char *key, const IrcDisplayField *fields, size_t count)
{
    if (!irc_display_render_lookup(out, cap, key, fields, count, c->translate))
        (void)irc_display_render_lookup(out, cap, key, fields, count, NULL);
}

static void list_network_channels(IdleCommands *c, App *a)
{
    app_list_sort(a, a->list.sort, a->list.descending);
    size_t count = app_list_visible_count(a);
    const char *sort = a->list.sort == APP_LIST_SORT_CHANNEL ? "channel" :
                       a->list.sort == APP_LIST_SORT_TOPIC ? "topic" : "users";
    char number_text[32], visible[32], cached[32], matched[32], received[32], counts[512], order[256];
    (void)snprintf(number_text, sizeof number_text, "%zu", server_label(c, c->client->current));
    (void)snprintf(visible, sizeof visible, "%zu", count);
    (void)snprintf(cached, sizeof cached, "%zu", a->list.count);
    (void)snprintf(matched, sizeof matched, "%zu", a->list.matched);
    (void)snprintf(received, sizeof received, "%zu", a->list.received);
    render_reader_fields(c, counts, sizeof counts, "showing {visible}, {cached} cached, {matched} matched, {received} received", (const IrcDisplayField[]){{"visible", visible}, {"cached", cached}, {"matched", matched}, {"received", received}}, 4);
    render_reader_fields(c, order, sizeof order, "sort {sort} {direction}", (const IrcDisplayField[]){{"sort", sort}, {"direction", a->list.descending ? "desc" : "asc"}}, 2);
    emit_fields(c, "Network channel directory on server {number}: {counts} | {order}{receiving}", (const IrcDisplayField[]){{"number", number_text}, {"counts", counts}, {"order", order}, {"receiving", a->list.active ? display_text(c, " | still receiving") : ""}}, 4);
    for (size_t i = 0; i < count; ++i) {
        const ChannelListEntry *entry = &a->list.entries[i];
        char users[32];
        (void)snprintf(users, sizeof users, "%llu", (unsigned long long)entry->users);
        emit_fields(c, "  {channel} | {count} users | {topic}", (const IrcDisplayField[]){{"channel", entry->channel}, {"count", users}, {"topic", *entry->topic ? entry->topic : display_text(c, "(no topic)")}}, 3);
    }
    if (!count) emit(c, "%s", display_text(c, a->list.active ?
        "No matching channels received yet." :
        "No cached channels match the current LIST filters."));
    if (a->list.display_limit && a->list.count > count)
        emit_fields(c, "Display limited to {visible} of {cached} cached matches.", (const IrcDisplayField[]){{"visible", visible}, {"cached", cached}}, 2);
    if (a->list.truncated) {
        char limit[32];
        (void)snprintf(limit, sizeof limit, "%u", (unsigned)APP_LIST_MAX);
        emit_fields(c, "Channel cache reached its hard safety limit of {limit} entries.", (const IrcDisplayField[]){{"limit", limit}}, 1);
    }
}

static void list_queries(IdleCommands *c, App *a)
{
    size_t count = 0;
    char number_text[32], counts[512];
    (void)snprintf(number_text, sizeof number_text, "%zu", server_label(c, c->client->current));
    emit_fields(c, "Queries on server {number}:", (const IrcDisplayField[]){{"number", number_text}}, 1);
    for (size_t i = 3; i < a->buffer_count; ++i) {
        const Buffer *b = &a->buffers[i];
        if (b->kind != B_QUERY) continue;
        (void)snprintf(number_text, sizeof number_text, "%zu", ++count);
        conversation_counts(c, b, counts, sizeof counts);
        emit_fields(c, "  {number}: {name}{selected} | {counts}", (const IrcDisplayField[]){{"number", number_text}, {"name", b->name}, {"selected", a->current == i ? display_text(c, " (selected)") : ""}, {"counts", counts}}, 4);
    }
    if (!count) emit(c, "%s", display_text(c, "No private queries. Use /pm NICK to open one without sending."));
}

static void list_servers(IdleCommands *c)
{
    emit(c, "%s", display_text(c, "Servers:"));
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        const ClientSlot *slot = &c->client->slots[i];
        if (!slot->used) continue;
        Config cfg;
        if (slot->app) app_config_effective(slot->app, &cfg); else cfg = slot->seed;
        char number_text[32], endpoint[512], details[1024];
        (void)snprintf(number_text, sizeof number_text, "%zu", server_label(c, i));
        (void)snprintf(endpoint, sizeof endpoint, "%s:%d", *cfg.server ? cfg.server : display_text(c, "(not configured)"), cfg.port);
        render_reader_fields(c, details, sizeof details, "nick {nick} | {status} | auto-connect {state} | {saved}", (const IrcDisplayField[]){{"nick", *cfg.nick ? cfg.nick : display_text(c, "(default)")}, {"status", slot->app ? app_connection_label(slot->app) : display_text(c, slot->load_failed ? "profile error" : "disconnected")}, {"state", display_text(c, cfg.autoconnect ? "on" : "off")}, {"saved", display_text(c, slot->saved ? "saved" : "unsaved")}}, 4);
        emit_fields(c, "  {number}: {endpoint}{selected} | {details}", (const IrcDisplayField[]){{"number", number_text}, {"endpoint", endpoint}, {"selected", i == c->client->current ? display_text(c, " (selected)") : ""}, {"details", details}}, 4);
        config_wipe(&cfg, sizeof cfg);
    }
}

static void list_unread(IdleCommands *c)
{
    size_t count = 0;
    emit(c, "%s", display_text(c, "Unread conversations:"));
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
        const App *a = c->client->slots[i].app;
        if (!a) continue;
        for (size_t j = 0; j < a->buffer_count; ++j) {
            const Buffer *b = &a->buffers[j];
            if (b->kind == B_HELP || b->kind == B_MENTIONS || (!b->unread && !b->unread_mentions)) continue;
            char number_text[32], counts[512];
            (void)snprintf(number_text, sizeof number_text, "%zu", server_label(c, i));
            conversation_counts(c, b, counts, sizeof counts);
            emit_fields(c, "  server {number} | {name} | {counts}", (const IrcDisplayField[]){{"number", number_text}, {"name", b->name}, {"counts", counts}}, 3);
            ++count;
        }
    }
    if (!count) emit(c, "%s", display_text(c, "No unread conversations."));
}

static int roster(IdleCommands *c, App *a, const char *rest, const Command *command)
{
    const Buffer *b = &a->buffers[a->current];
    if (*rest) {
        Word name = word(rest); char channel[IRC_TARGET_MAX + 1];
        if (*name.rest || !copy_word(channel, sizeof channel, name) || !irc_valid_channel(channel)) return usage(c, command);
        size_t index = app_buffer(a, channel, B_CHANNEL, 0);
        if (index == SIZE_MAX) { emit(c, "%s", display_text(c, "No retained channel with that name. Use /channels.")); return IDLE_COMMAND_ERROR; }
        b = &a->buffers[index];
    }
    if (b->kind != B_CHANNEL) { emit(c, "%s", display_text(c, "Select a channel or use /users #channel.")); return IDLE_COMMAND_ERROR; }
    char count_text[32];
    (void)snprintf(count_text, sizeof count_text, "%zu", b->user_count);
    emit_fields(c, "Users in {channel}: {count}{incomplete}{limit}", (const IrcDisplayField[]){{"channel", b->name}, {"count", count_text}, {"incomplete", b->names_in_progress ? display_text(c, " (NAMES incomplete)") : ""}, {"limit", b->names_truncated ? display_text(c, " (roster limit reached)") : ""}}, 4);
    for (size_t i = 0; i < b->user_count; ++i) {
        char modes[17]; size_t n = 0;
        for (size_t j = 0; j < 16 && a->prefix_chars[j]; ++j)
            if (b->users[i].modes & (1u << j)) modes[n++] = a->prefix_chars[j];
        modes[n] = 0; emit(c, "  %s%s", modes, b->users[i].nick);
    }
    return IDLE_COMMAND_DONE;
}

static const char *secret_source(IdleCommands *c, const char *plain, const char *file, const char *gpg)
{
    return display_text(c, *gpg ? "GPG file (redacted)" : *file ? "private file (redacted)" : *plain ? "plaintext config value (redacted)" : "none");
}

static void auth_status(IdleCommands *c, const Config *cfg)
{
    if (cfg->bouncer_type != CONFIG_BOUNCER_NONE) {
        char identity[192];
        const char *login = config_bouncer_identity(cfg, identity, sizeof identity) == 1 ? identity : display_text(c, "(incomplete)");
        emit_fields(c, "Bouncer: {type} | login: {login} | automatic away: {away}", (const IrcDisplayField[]){{"type", cfg->bouncer_type == CONFIG_BOUNCER_ZNC ? "ZNC" : "soju"}, {"login", login}, {"away", display_text(c, cfg->bouncer_away ? "enabled for this profile" : "off")}}, 3);
        emit_fields(c, "Bouncer credential: {source}", (const IrcDisplayField[]){{"source", cfg->bouncer_type == CONFIG_BOUNCER_ZNC ? secret_source(c, cfg->server_password, cfg->server_password_file, cfg->server_password_gpg_file) : secret_source(c, cfg->password, cfg->password_file, cfg->password_gpg_file)}}, 1);
        return;
    }
    emit_fields(c, "Auto-auth: {mode} | account: {account} | credential: {source}", (const IrcDisplayField[]){{"mode", !strcmp(cfg->auth, "none") ? display_text(c, "none") : cfg->auth}, {"account", *cfg->auth_user ? cfg->auth_user : display_text(c, "(default nickname)")}, {"source", secret_source(c, cfg->password, cfg->password_file, cfg->password_gpg_file)}}, 3);
}

static void startup_state(IdleCommands *c, const App *a, const Config *next)
{
    char number[32], port[32];
    (void)snprintf(number, sizeof number, "%zu", server_label(c, c->client->current));
    (void)snprintf(port, sizeof port, "%d", a->cfg.port);
    emit_fields(c, "Server {number}: {host}:{port} ({transport})", (const IrcDisplayField[]){{"number", number}, {"host", *a->cfg.server ? a->cfg.server : display_text(c, "not configured")}, {"port", port}, {"transport", display_text(c, a->cfg.tls ? "verified TLS" : "plaintext")}}, 4);
    emit_fields(c, "Connection: {status} | registered: {registered}", (const IrcDisplayField[]){{"status", app_connection_label(a)}, {"registered", display_text(c, a->registered ? "yes" : "no")}}, 2);
    if (a->test_mode) emit(c, "%s", display_text(c, "Authentication: bypassed in offline test mode."));
    else if (!strcmp(a->cfg.auth, "sasl"))
        emit_fields(c, "Required SASL: {status}", (const IrcDisplayField[]){{"status", display_text(c, a->authenticated ? "succeeded" : "not authenticated")}}, 1);
    else emit(c, "%s", display_text(c, "Required SASL: off"));
    if (a->credential) emit(c, "%s", display_text(c, "GPG credential decryption is pending; registration and autojoin wait."));
    if (strcmp(a->cfg.server, next->server) || a->cfg.port != next->port || a->cfg.tls != next->tls) {
        (void)snprintf(port, sizeof port, "%d", next->port);
        emit_fields(c, "Next explicit /connect endpoint: {host}:{port} ({transport})", (const IrcDisplayField[]){{"host", *next->server ? next->server : display_text(c, "not configured")}, {"port", port}, {"transport", display_text(c, next->tls ? "verified TLS" : "plaintext")}}, 3);
    }
}

/* A configured path can be much longer than a status or emit buffer. Keep
 * every byte recoverable in bounded labeled pieces. Escaping also prevents
 * invalid UTF-8 or formatting bytes from changing the displayed filename. */
static void credential_path(IdleCommands *c, const char *label, const char *path)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t length = strlen(path), offset = 0;
    int escaped = 0;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)path[i] < 32 || (unsigned char)path[i] >= 127 || path[i] == '\\') escaped = 1;
    if (escaped) emit_fields(c, "{label} display uses {backslash} for a backslash and {hex} for other escaped bytes.", (const IrcDisplayField[]){{"label", label}, {"backslash", "\\\\"}, {"hex", "\\xHH"}}, 3);
    while (offset < length) {
        char part[385];
        size_t used = 0, start = offset;
        while (offset < length) {
            unsigned char byte = (unsigned char)path[offset];
            size_t need = byte == '\\' ? 2u : byte < 32 || byte >= 127 ? 4u : 1u;
            if (need > sizeof part - 1 - used) break;
            if (need == 1) part[used++] = (char)byte;
            else if (need == 2) { part[used++] = '\\'; part[used++] = '\\'; }
            else {
                part[used++] = '\\'; part[used++] = 'x';
                part[used++] = hex[byte >> 4]; part[used++] = hex[byte & 15];
            }
            ++offset;
        }
        part[used] = 0;
        if (!start && offset == length) emit(c, "%s: %s", label, part);
        else {
            char range[64], total[32];
            (void)snprintf(range, sizeof range, "%zu-%zu", start + 1, offset);
            (void)snprintf(total, sizeof total, "%zu", length);
            emit_fields(c, "{label} [bytes {range} of {total}]: {path}", (const IrcDisplayField[]){{"label", label}, {"range", range}, {"total", total}, {"path", part}}, 4);
        }
    }
}

static void auth_paths(IdleCommands *c, const Config *cfg, const char *prefix)
{
    const char *paths[] = {cfg->password_file, cfg->password_gpg_file,
                          cfg->server_password_file, cfg->server_password_gpg_file};
    static const char *const names[] = {"SASL password file", "SASL GPG file",
                                       "Server-password file", "Server-password GPG file"};
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        if (!*paths[i]) continue;
        /* Translate the whole prefix template so each language can order the label. */
        const char *name = display_text(c, names[i]);
        char label[512];
        const IrcDisplayField field[] = {{"label", name}};
        if (*prefix && irc_display_render_lookup(label, sizeof label, prefix, field, 1, c->translate))
            credential_path(c, label, paths[i]);
        else credential_path(c, name, paths[i]);
    }
}

static int auth_changed(const Config *a, const Config *b)
{
    return a->bouncer_type != b->bouncer_type || a->bouncer_away != b->bouncer_away ||
           strcmp(a->bouncer_user, b->bouncer_user) ||
           strcmp(a->bouncer_network, b->bouncer_network) ||
           strcmp(a->bouncer_client, b->bouncer_client) ||
           strcmp(a->auth, b->auth) || strcmp(a->auth_user, b->auth_user) ||
           strcmp(a->password, b->password) || strcmp(a->password_file, b->password_file) ||
           strcmp(a->password_gpg_file, b->password_gpg_file) ||
           strcmp(a->server_password, b->server_password) ||
           strcmp(a->server_password_file, b->server_password_file) ||
           strcmp(a->server_password_gpg_file, b->server_password_gpg_file);
}

static void auth_inquiry(IdleCommands *c, const App *a, const Config *next)
{
    int changed = auth_changed(&a->cfg, next);
    startup_state(c, a, next);
    emit(c, "%s", display_text(c, changed ? "Current connection authentication settings:" : "Authentication settings:"));
    auth_status(c, &a->cfg);
    emit_fields(c, "Server password: {source}", (const IrcDisplayField[]){{"source", secret_source(c, a->cfg.server_password, a->cfg.server_password_file, a->cfg.server_password_gpg_file)}}, 1);
    auth_paths(c, &a->cfg, changed ? "Current {label}" : "");
    if (changed) {
        emit(c, "%s", display_text(c, "Pending authentication for the next explicit /connect:"));
        auth_status(c, next);
        emit_fields(c, "Next server password: {source}", (const IrcDisplayField[]){{"source", secret_source(c, next->server_password, next->server_password_file, next->server_password_gpg_file)}}, 1);
        auth_paths(c, next, "Next {label}");
        emit(c, "%s", display_text(c, "Automatic retries keep current authentication. /disconnect then /connect applies pending settings; /save persists them."));
    }
    emit(c, "%s", display_text(c, "File paths are shown only by this explicit inquiry. No credential file was read and no password content is displayed."));
}

static void autojoin_inquiry(IdleCommands *c, App *a, const Config *next)
{
    startup_state(c, a, next);
    if (a->cfg.bouncer_type != CONFIG_BOUNCER_NONE || next->bouncer_type != CONFIG_BOUNCER_NONE) {
        emit(c, "%s", display_text(c, "Bouncer profiles restore their own channel memberships; saved auto-join lists are not replayed."));
        emit(c, "%s", display_text(c, "Use /join and /part while connected, or /channels to inspect restored channels."));
        return;
    }
    const char *running = a->test_mode ? a->test_channels : a->cfg.channels;
    emit_fields(c, "Running auto-join: {channels}", (const IrcDisplayField[]){{"channels", *running ? running : display_text(c, "(none)")}}, 1);
    int changed = strcmp(a->cfg.channels, next->channels) != 0;
    if (changed) emit_fields(c, "Pending auto-join for next explicit /connect: {channels}", (const IrcDisplayField[]){{"channels", *next->channels ? next->channels : display_text(c, "(none)")}}, 1);
    char channels[sizeof a->cfg.channels];
    strcpy(channels, running);
    for (char *name = channels, *end; *name; name = end + 1) {
        end = strchr(name, ',');
        if (end) *end = 0;
        size_t view = app_buffer(a, name, B_CHANNEL, 0);
        emit_fields(c, "Channel {channel}: {status}", (const IrcDisplayField[]){{"channel", name}, {"status", display_text(c, view != SIZE_MAX && a->buffers[view].joined ? "joined" : "not joined")}}, 2);
        if (!end) break;
    }
    if (!a->registered) {
        if (!a->test_mode && !strcmp(a->cfg.auth, "sasl") && !a->authenticated)
            emit(c, "%s", display_text(c, "Autojoin waits for required SASL authentication and successful IRC registration."));
        else emit(c, "%s", display_text(c, "Autojoin waits for successful IRC registration."));
    } else emit(c, "%s", display_text(c, "Join requests use the running list after registration. Channels still not joined may be pending or rejected; see this server's replies."));
    if (changed) emit(c, "%s", display_text(c, "Pending channels do not change this connection or automatic retries. /disconnect then /connect applies them; /save persists them."));
    else emit(c, "%s", display_text(c, "Use /join for an immediate join on a registered connection. /autojoin add/remove/clear prepares the next explicit /connect."));
}

static int settings(IdleCommands *c, App *a)
{
    Config cfg; app_config_effective(a, &cfg);
    const ClientSlot *primary = &c->client->slots[0];
    int review = primary->app ? primary->app->cfg.send_review : primary->seed.send_review;
    int full = primary->app ? primary->app->cfg.output_full : primary->seed.output_full;
    int queue = primary->app ? primary->app->cfg.queue_notices : primary->seed.queue_notices;
    int program_messages = primary->app ? primary->app->cfg.program_messages : primary->seed.program_messages;
    int persistent_logging = primary->app ? primary->app->cfg.persistent_logging : primary->seed.persistent_logging;
    int single_session = primary->app ? primary->app->cfg.single_session : primary->seed.single_session;
    int default_interface = primary->app ? primary->app->cfg.default_interface : primary->seed.default_interface;
    int minimal = primary->app ? primary->app->cfg.minimal : primary->seed.minimal;
    int history = primary->app ? primary->app->cfg.history_lines : primary->seed.history_lines;
    int suggestions = primary->app ? primary->app->cfg.command_suggestions : primary->seed.command_suggestions;
    int composer_fullscreen = primary->app ? primary->app->cfg.composer_fullscreen : primary->seed.composer_fullscreen;
    emit(c, "%s", display_text(c, "Application preferences:"));
    char hist[32], number[32], port[32], count[32], delay[32], interval[32], watch[32], ignore[32];
    (void)snprintf(hist, sizeof hist, "%d", history);
    emit_fields(c, "History lines: {count} | send mode: {mode}", (const IrcDisplayField[]){{"count", hist}, {"mode", review ? "review" : "direct"}}, 2);
    emit_fields(c, "Output format: {format} | queue notices: {queue} | program messages: {messages}", (const IrcDisplayField[]){{"format", full ? "full" : "compact"}, {"queue", display_text(c, queue ? "on" : "off")}, {"messages", display_text(c, program_messages ? "on" : "off")}}, 3);
    emit_fields(c, "Persistent logging: {logging} | single-session mode: {session}", (const IrcDisplayField[]){{"logging", display_text(c, persistent_logging ? "on" : "off")}, {"session", display_text(c, single_session ? "on" : "off")}}, 2);
    if (minimal) emit_fields(c, "Default interface: idle (fixed in onekeyirc; saved preference: {interface})", (const IrcDisplayField[]){{"interface", default_interface == CONFIG_INTERFACE_IDLE ? "idle" : "ncurses"}}, 1);
    else emit_fields(c, "Default interface: {interface} (next fresh launch)", (const IrcDisplayField[]){{"interface", default_interface == CONFIG_INTERFACE_IDLE ? "idle" : "ncurses"}}, 1);
    emit_fields(c, "Command suggestions: {suggestions} | Composer mode: {mode}", (const IrcDisplayField[]){{"suggestions", commands_suggestion_mode_name(suggestions)}, {"mode", composer_fullscreen ? "fullscreen" : "line"}}, 2);
    char minutes[32];
    (void)snprintf(minutes, sizeof minutes, "%d", cfg.autoaway_minutes);
    emit_fields(c, cfg.autoaway_minutes == 1 ? "Presence: background-away {state} | auto-away {minutes} minute" :
                "Presence: background-away {state} | auto-away {minutes} minutes",
                (const IrcDisplayField[]){{"state", display_text(c, cfg.background_away ? "on" : "off")}, {"minutes", minutes}}, 2);
    emit_fields(c, "PART default: {part} | QUIT default: {quit}", (const IrcDisplayField[]){{"part", *cfg.part_message ? cfg.part_message : display_text(c, "(none)")}, {"quit", *cfg.quit_message ? cfg.quit_message : display_text(c, "(none)")}}, 2);
    emit_fields(c, "Background away: {background} | Idle away: {idle}", (const IrcDisplayField[]){{"background", cfg.background_away_message}, {"idle", cfg.autoaway_message}}, 2);
    (void)snprintf(number, sizeof number, "%zu", server_label(c, c->client->current));
    emit_fields(c, "Effective settings for server {number} (connection edits apply on the next disconnected /connect):", (const IrcDisplayField[]){{"number", number}}, 1);
    (void)snprintf(port, sizeof port, "%d", cfg.port);
    emit_fields(c, "Server: {host} | port: {port} | verified TLS: {tls}", (const IrcDisplayField[]){{"host", *cfg.server ? cfg.server : display_text(c, "(not configured)")}, {"port", port}, {"tls", display_text(c, cfg.tls ? "on" : "off")}}, 3);
    emit_fields(c, "Nickname: {nick} | username: {user} | real name: {real}", (const IrcDisplayField[]){{"nick", cfg.nick}, {"user", cfg.username}, {"real", cfg.realname}}, 3);
    emit_fields(c, "Auto-connect: {state} | auto-join: {channels}", (const IrcDisplayField[]){{"state", display_text(c, cfg.autoconnect ? "on" : "off")}, {"channels", cfg.bouncer_type != CONFIG_BOUNCER_NONE ? display_text(c, "managed by bouncer") : *cfg.channels ? cfg.channels : display_text(c, "(none)")}}, 2);
    (void)snprintf(count, sizeof count, "%zu", cfg.autocommands.count);
    (void)snprintf(delay, sizeof delay, "%d", cfg.autocommands.delay_seconds);
    (void)snprintf(interval, sizeof interval, "%d", cfg.autocommands.interval_seconds);
    emit_fields(c, "Automatic commands: {state} | {count} entries | delay {delay} seconds | interval {interval} seconds", (const IrcDisplayField[]){{"state", display_text(c, cfg.autocommands.enabled ? "on" : "off")}, {"count", count}, {"delay", delay}, {"interval", interval}}, 4);
    auth_status(c, &cfg);
    emit_fields(c, "Server password: {source}", (const IrcDisplayField[]){{"source", secret_source(c, cfg.server_password, cfg.server_password_file, cfg.server_password_gpg_file)}}, 1);
    (void)snprintf(watch, sizeof watch, "%zu", cfg.word_count);
    (void)snprintf(ignore, sizeof ignore, "%zu", cfg.ignore_count);
    emit_fields(c, "CA: {source} | watch strings: {watch} | ignore masks: {ignore}", (const IrcDisplayField[]){{"source", display_text(c, *cfg.ca_file ? "explicit file" : "system trust")}, {"watch", watch}, {"ignore", ignore}}, 3);
    config_wipe(&cfg, sizeof cfg); return IDLE_COMMAND_DONE;
}

static int apply(IdleCommands *c, App *a, Config *cfg, const char *message)
{
    char error[512];
    int okay = app_config_apply(a, cfg, 0, error, sizeof error);
    config_wipe(cfg, sizeof *cfg);
    if (!okay) { emit_fields(c, "Settings unchanged: {detail}", (const IrcDisplayField[]){{"detail", display_config_error(c, error, (char[1024]){0}, 1024)}}, 1); return IDLE_COMMAND_ERROR; }
    emit(c, "%s", display_text(c, message)); return IDLE_COMMAND_DONE;
}

static int startup_command(IdleCommands *c, App *a, const Command *command, const char *rest)
{
    Config cfg; app_config_effective(a, &cfg);
    Word action = word(rest);
    int result = IDLE_COMMAND_ERROR;
    if (!strcmp(command->spec.name, "autoconnect")) {
        if (!action.size) { emit_fields(c, "Auto-connect: {state}", (const IrcDisplayField[]){{"state", display_text(c, cfg.autoconnect ? "on" : "off")}}, 1); result = IDLE_COMMAND_DONE; }
        else if (!*action.rest && (equal(action, "on") || equal(action, "off"))) {
            cfg.autoconnect = equal(action, "on");
            return apply(c, a, &cfg, "Auto-connect changed for next startup. /save persists it; /connect acts now.");
        } else result = usage(c, command);
    } else if (!strcmp(command->spec.name, "autoauth")) {
        if (!action.size || (equal(action, "status") && !*action.rest)) { auth_inquiry(c, a, &cfg); result = IDLE_COMMAND_DONE; }
        else if (cfg.bouncer_type != CONFIG_BOUNCER_NONE)
            emit(c, "%s", display_text(c, "Use /panel to change this bouncer's login or password source; /autoauth status shows its settings."));
        else if (equal(action, "off") && !*action.rest) {
            strcpy(cfg.auth, "none"); cfg.auth_user[0] = 0;
            config_wipe(cfg.password, sizeof cfg.password);
            config_wipe(cfg.password_file, sizeof cfg.password_file);
            config_wipe(cfg.password_gpg_file, sizeof cfg.password_gpg_file);
            return apply(c, a, &cfg, "Auto-auth disabled for the next connection. Existing authentication is unchanged; /save persists it.");
        } else if (equal(action, "sasl")) {
            Word user = word(action.rest), source = word(user.rest);
            const char *path = source.rest;
            if (!copy_word(cfg.auth_user, sizeof cfg.auth_user, user) ||
                (!equal(source, "file") && !equal(source, "gpg")) || !*path || strlen(path) >= CONFIG_PATH_MAX) result = usage(c, command);
            else {
                strcpy(cfg.auth, "sasl");
                config_wipe(cfg.password, sizeof cfg.password);
                config_wipe(cfg.password_file, sizeof cfg.password_file);
                config_wipe(cfg.password_gpg_file, sizeof cfg.password_gpg_file);
                strcpy(equal(source, "gpg") ? cfg.password_gpg_file : cfg.password_file, path);
                return apply(c, a, &cfg, "SASL source staged for the next connection; credential not read. /save persists it.");
            }
        } else result = usage(c, command);
    } else {
        if (!action.size || (equal(action, "list") && !*action.rest)) {
            autojoin_inquiry(c, a, &cfg); result = IDLE_COMMAND_DONE;
        } else if (cfg.bouncer_type != CONFIG_BOUNCER_NONE) {
            emit(c, "%s", display_text(c, "This bouncer manages channel membership. Use /join or /part while connected, or /panel to change connection type."));
        } else if (equal(action, "clear") && !*action.rest) {
            cfg.channels[0] = 0;
            return apply(c, a, &cfg, "Auto-join list cleared for the next explicit /connect. Live channels and automatic retries keep the running list; /save persists it.");
        } else if (equal(action, "add") || equal(action, "remove")) {
            Word value = word(action.rest); char channel[IRC_TARGET_MAX + 1];
            if (*value.rest || !copy_word(channel, sizeof channel, value) || !irc_valid_channel(channel)) result = usage(c, command);
            else {
                char current[sizeof cfg.channels], next[sizeof cfg.channels] = "";
                strcpy(current, cfg.channels);
                int found = 0; size_t used = 0;
                for (char *part = current, *end; *part; part = end + 1) {
                    end = strchr(part, ','); if (end) *end = 0;
                    int same = irc_equal(part, channel, a->casemap);
                    if (same) found = 1;
                    if (!same || equal(action, "add")) {
                        size_t n = strlen(part);
                        if (used) next[used++] = ',';
                        memcpy(next + used, part, n + 1); used += n;
                    }
                    if (!end) break;
                }
                if (equal(action, "remove") && !found) emit(c, "%s", display_text(c, "That channel is not in the auto-join list; nothing changed."));
                else if (equal(action, "add") && !found && used + (used != 0) + strlen(channel) >= sizeof next)
                    emit(c, "%s", display_text(c, "Auto-join list is too long; nothing changed."));
                else {
                    if (equal(action, "add") && !found) {
                        if (used) next[used++] = ',';
                        strcpy(next + used, channel);
                    }
                    strcpy(cfg.channels, next);
                    return apply(c, a, &cfg, "Auto-join list staged. /disconnect then /connect applies it; automatic retries keep the running list. /save persists it.");
                }
            }
        } else result = usage(c, command);
    }
    config_wipe(&cfg, sizeof cfg); return result;
}

static int autocommands_command(IdleCommands *c, App *a, const Command *command, const char *rest)
{
    Config cfg; app_config_effective(a, &cfg);
    ConfigAutocommands *list = &cfg.autocommands;
    Word action = word(rest);
    int result = IDLE_COMMAND_ERROR, changed = 0;
    char error[512];
    if (!action.size || (equal(action, "list") && !*action.rest)) {
        char count_text[64], delay_text[32], interval_text[32];
        (void)snprintf(count_text, sizeof count_text, "%zu/%d", list->count, CONFIG_AUTOCOMMAND_MAX);
        (void)snprintf(delay_text, sizeof delay_text, "%d", list->delay_seconds);
        (void)snprintf(interval_text, sizeof interval_text, "%d", list->interval_seconds);
        emit_fields(c, "Automatic commands: {state} | {count} | delay {delay} seconds | interval {interval} seconds", (const IrcDisplayField[]){{"state", display_text(c, list->enabled ? "on" : "off")}, {"count", count_text}, {"delay", delay_text}, {"interval", interval_text}}, 4);
        for (size_t i = 0; i < list->count; ++i) {
            ConfigAutoCommand parsed;
            if (config_autocommand_parse(list->lines[i], &parsed, NULL, 0) > 0) {
                static const char *const names[] = {"msg", "join", "nick", "away", "back", "whois"};
                emit(c, "  %zu: /%s%s%s%s", i + 1, names[parsed.kind],
                     *parsed.target ? " " : "", parsed.target,
                     *parsed.text ? display_text(c, " [text hidden]") : "");
            }
            config_wipe(&parsed, sizeof parsed);
        }
        result = IDLE_COMMAND_DONE;
    } else if (equal(action, "add")) {
        if (list->count >= CONFIG_AUTOCOMMAND_MAX) {
            char limit[32];
            (void)snprintf(limit, sizeof limit, "%d", CONFIG_AUTOCOMMAND_MAX);
            emit_fields(c, "Automatic command list is full ({limit}); remove an entry first.", (const IrcDisplayField[]){{"limit", limit}}, 1);
        } else if (config_autocommand_parse(action.rest, NULL, error, sizeof error) < 0)
            emit(c, "%s", display_config_error(c, error, (char[1024]){0}, 1024));
        else {
            memcpy(list->lines[list->count++], action.rest, strlen(action.rest) + 1u);
            changed = 1;
        }
    } else if ((equal(action, "on") || equal(action, "off") || equal(action, "clear")) && !*action.rest) {
        if (equal(action, "clear")) {
            config_wipe(list->lines, sizeof list->lines); list->count = 0;
        } else list->enabled = equal(action, "on");
        changed = 1;
    } else if (equal(action, "remove") || equal(action, "delay") || equal(action, "interval")) {
        Word value = word(action.rest);
        size_t n = 0;
        if (*value.rest || !number(value, 300, &n)) result = usage(c, command);
        else if (equal(action, "remove")) {
            if (!n || n > list->count) emit(c, "%s", display_text(c, "That automatic command number does not exist. Use /autocommands list."));
            else {
                if (n < list->count) memmove(list->lines[n - 1], list->lines[n], (list->count - n) * sizeof list->lines[0]);
                config_wipe(list->lines[--list->count], sizeof list->lines[0]);
                changed = 1;
            }
        } else if (equal(action, "delay")) { list->delay_seconds = (int)n; changed = 1; }
        else if (n < 2 || n > 60) emit(c, "%s", display_text(c, "Automatic command interval must be from 2 to 60 seconds."));
        else { list->interval_seconds = (int)n; changed = 1; }
    } else result = usage(c, command);
    if (changed) return apply(c, a, &cfg, "Automatic commands staged for the next connection; /save persists them. /cancel stops this connection's pending commands.");
    config_wipe(&cfg, sizeof cfg);
    return result;
}

typedef struct ConnectRequest {
    char host[256], nick[64];
    int port, tls, have_port, have_tls, have_nick;
} ConnectRequest;

static int same_host(const char *left, const char *right)
{
    unsigned char a[16], b[16];
    if (strchr(left, ':') && strchr(right, ':') &&
        inet_pton(AF_INET6, left, a) == 1 && inet_pton(AF_INET6, right, b) == 1)
        return !memcmp(a, b, sizeof a);
    while (*left && *right) {
        if (lower((unsigned char)*left++) != lower((unsigned char)*right++)) return 0;
    }
    return !*left && !*right;
}

static int digits(Word w)
{
    if (!w.size) return 0;
    for (size_t i = 0; i < w.size; ++i)
        if (w.text[i] < '0' || w.text[i] > '9') return 0;
    return 1;
}

static int connect_parse(IdleCommands *c, const Command *command, const char *rest,
                          ConnectRequest *request)
{
    Word w = word(rest);
    memset(request, 0, sizeof *request);
    while (w.size && w.text[0] == '-') {
        if (equal(w, "-tls") || equal(w, "--tls") || equal(w, "-ssl") || equal(w, "--ssl") ||
            equal(w, "-plain") || equal(w, "--plain")) {
            if (request->have_tls) return usage(c, command);
            request->tls = !(equal(w, "-plain") || equal(w, "--plain"));
            request->have_tls = 1;
        } else if (equal(w, "-nick") || equal(w, "--nick")) {
            if (request->have_nick) return usage(c, command);
            w = word(w.rest);
            if (!copy_word(request->nick, sizeof request->nick, w) ||
                !irc_valid_nick(request->nick)) {
                emit(c, "%s", display_text(c, "Nickname must be a valid IRC nickname; use -nick NICK before the hostname."));
                return IDLE_COMMAND_ERROR;
            }
            request->have_nick = 1;
        } else return usage(c, command);
        w = word(w.rest);
    }
    if (!copy_word(request->host, sizeof request->host, w)) return usage(c, command);
    size_t host_length = strlen(request->host);
    if (host_length > 2 && request->host[0] == '[' && request->host[host_length - 1] == ']' &&
        strchr(request->host, ':')) {
        memmove(request->host, request->host + 1, host_length - 2);
        request->host[host_length - 2] = 0;
    }
    w = word(w.rest);
    if (w.size) {
        if (w.text[0] == '+') {
            if (request->have_tls && !request->tls) {
                emit(c, "%s", display_text(c, "A +PORT requests TLS and cannot be combined with -plain."));
                return IDLE_COMMAND_ERROR;
            }
            request->have_tls = request->tls = 1;
            ++w.text; --w.size;
        }
        size_t value;
        if (!number(w, 65535, &value) || !value) {
            emit(c, "%s", display_text(c, "Port must be a number from 1 to 65535; use -nick NICK for a nickname."));
            return IDLE_COMMAND_ERROR;
        }
        request->have_port = 1;
        request->port = (int)value;
        if (*w.rest) {
            emit(c, "%s", display_text(c, "Extra connection arguments rejected. Use -nick NICK; passwords are configured with /autoauth or /panel."));
            return IDLE_COMMAND_ERROR;
        }
    }
    return IDLE_COMMAND_DONE;
}

static void connect_options(Config *cfg, const ConnectRequest *request)
{
    (void)snprintf(cfg->server, sizeof cfg->server, "%s", request->host);
    if (request->have_tls) {
        if (!request->have_port && cfg->tls != request->tls && (cfg->port == 6697 || cfg->port == 6667))
            cfg->port = request->tls ? 6697 : 6667;
        cfg->tls = request->tls;
    }
    if (request->have_port) cfg->port = request->port;
    if (request->have_nick) (void)snprintf(cfg->nick, sizeof cfg->nick, "%s", request->nick);
}

static int connect_matches(const Config *cfg, const ConnectRequest *request)
{
    return same_host(cfg->server, request->host) &&
           (!request->have_port || cfg->port == request->port) &&
           (!request->have_tls || cfg->tls == request->tls) &&
           (!request->have_nick || !strcmp(cfg->nick, request->nick));
}

static int connect_slot(IdleCommands *c, size_t index, int explicit_choice)
{
    char error[512];
    App *next = client_get_app(c->client, index, error, sizeof error);
    if (!next) { emit(c, "%s", error); return IDLE_COMMAND_ERROR; }
    Config cfg; app_config_effective(next, &cfg);
    int configured = next->net || next->sim ? *next->cfg.server != 0 : *cfg.server != 0;
    config_wipe(&cfg, sizeof cfg);
    if (!configured) {
        emit(c, "%s", display_text(c, "No server is configured. Use /connect irc.example.org, then /join #channel."));
        return IDLE_COMMAND_ERROR;
    }
    App *old = c->client->slots[c->client->current].app;
    if (!c->defer_visibility && old && old != next) app_set_visible(old, 0);
    c->client->current = index;
    if (explicit_choice) c->client->default_unavailable = 0;
    if (!c->defer_visibility) {
        app_switch(next, 0);
        app_set_visible(next, 1);
    }
    char port_text[32];
    (void)snprintf(port_text, sizeof port_text, "%d", next->cfg.port);
    if (next->net || next->sim) {
        emit_fields(c, "Selected {server}:{port} as {nick} ({status}).", (const IrcDisplayField[]){{"server", next->cfg.server}, {"port", port_text}, {"nick", next->nick}, {"status", app_connection_label(next)}}, 4);
        return IDLE_COMMAND_DONE;
    }
    if (client_connect(c->client, index, error, sizeof error) < 0) {
        emit_fields(c, "Connection could not start: {detail}", (const IrcDisplayField[]){{"detail", error}}, 1);
        return IDLE_COMMAND_ERROR;
    }
    (void)snprintf(port_text, sizeof port_text, "%d", next->cfg.port);
    emit_fields(c, "Connecting to {server}:{port} as {nick}{transport}.", (const IrcDisplayField[]){{"server", next->cfg.server}, {"port", port_text}, {"nick", next->nick}, {"transport", display_text(c, next->test_mode ? " (offline simulation)" : next->cfg.tls ? " with verified TLS" : " with explicit plaintext transport")}}, 4);
    return IDLE_COMMAND_DONE;
}

static int connect_command(IdleCommands *c, const Command *command, const char *rest)
{
    Word first = word(rest);
    if (!first.size) {
        if (c->client->default_unavailable) {
            emit(c, "%s", display_text(c, "The saved default connection is unavailable. Use /connect HOST or /server NUMBER to choose one explicitly."));
            return IDLE_COMMAND_ERROR;
        }
        return connect_slot(c, c->client->current, 0);
    }
    if (digits(first)) {
        size_t index;
        if (!server_number(c, first, &index)) return IDLE_COMMAND_ERROR;
        return connect_slot(c, index, 1);
    }
    ConnectRequest request;
    if (!connect_parse(c, command, rest, &request)) {
        config_wipe(&request, sizeof request); return IDLE_COMMAND_ERROR;
    }
    Config candidate;
    client_new_config(c->client, &candidate);
    connect_options(&candidate, &request);
    char error[512];
    if (config_validate(&candidate, error, sizeof error) < 0) {
        emit_fields(c, "Connection settings are invalid: {detail}", (const IrcDisplayField[]){{"detail", display_config_error(c, error, (char[1024]){0}, 1024)}}, 1);
        config_wipe(&candidate, sizeof candidate); config_wipe(&request, sizeof request);
        return IDLE_COMMAND_ERROR;
    }

    size_t chosen = SIZE_MAX;
    /* Prefer a live endpoint before any disconnected profile. Pending edits
     * describe the next connection, never the identity of a live peer. */
    for (int live = 1; live >= 0 && chosen == SIZE_MAX; --live) {
        for (size_t pass = 0; pass <= CLIENT_MAX_SERVERS; ++pass) {
            size_t index = pass ? pass - 1 : c->client->current;
            if (pass && index == c->client->current) continue;
            ClientSlot *slot = &c->client->slots[index];
            if (!slot->used || slot->load_failed) continue;
            App *app = slot->app;
            int active = app && (app->net || app->sim);
            if (active != live) continue;
            Config available;
            if (active) {
                available = app->cfg;
                (void)snprintf(available.nick, sizeof available.nick, "%s", app->nick);
            } else if (app) {
                app_config_effective(app, &available);
                if (*app->cfg.server && (!same_host(app->cfg.server, available.server) ||
                    app->cfg.port != available.port || app->cfg.tls != available.tls)) {
                    config_wipe(&available, sizeof available); continue;
                }
            } else available = slot->seed;
            int matches = connect_matches(&available, &request);
            config_wipe(&available, sizeof available);
            if (matches) { chosen = index; break; }
        }
    }
    if (chosen == SIZE_MAX) {
        ClientSlot *primary = &c->client->slots[0];
        Config empty;
        if (primary->app) app_config_effective(primary->app, &empty); else empty = primary->seed;
        if (!*empty.server && (!primary->app || (!*primary->app->cfg.server &&
            !primary->app->net && !primary->app->sim))) {
            /* A not-yet-configured primary keeps deliberate --nick, /nick,
             * --channel and other first-connection options. No old peer exists. */
            config_wipe(&candidate, sizeof candidate);
            candidate = empty;
            connect_options(&candidate, &request);
            App *app = client_get_app(c->client, 0, error, sizeof error);
            if (!app || !app_config_apply(app, &candidate, 0, error, sizeof error)) {
                emit_fields(c, "Connection settings were not changed: {detail}", (const IrcDisplayField[]){{"detail", display_config_error(c, error, (char[1024]){0}, 1024)}}, 1);
                config_wipe(&empty, sizeof empty); config_wipe(&candidate, sizeof candidate);
                config_wipe(&request, sizeof request); return IDLE_COMMAND_ERROR;
            }
            chosen = 0;
        } else if (client_add(c->client, &candidate, &chosen, error, sizeof error) < 0) {
            emit_fields(c, "Connection could not be created: {detail}", (const IrcDisplayField[]){{"detail", error}}, 1);
            config_wipe(&empty, sizeof empty); config_wipe(&candidate, sizeof candidate);
            config_wipe(&request, sizeof request); return IDLE_COMMAND_ERROR;
        }
        config_wipe(&empty, sizeof empty);
    }
    config_wipe(&candidate, sizeof candidate); config_wipe(&request, sizeof request);
    return connect_slot(c, chosen, 1);
}

static int server_command(IdleCommands *c, App *a, const Command *command, const char *rest)
{
    Word action = word(rest);
    char error[512]; size_t index;
    if (!action.size) {
        if (!c->defer_visibility) app_switch(a, 0);
        return IDLE_COMMAND_DONE;
    }
    if (equal(action, "add")) {
        Word host = word(action.rest), port = word(host.rest), nick = word(port.rest);
        Config cfg; client_new_config(c->client, &cfg);
        cfg.autoconnect = 0;
        int okay = copy_word(cfg.server, sizeof cfg.server, host) && !*nick.rest;
        size_t value;
        if (port.size) {
            okay = okay && number(port, 65535, &value) && value != 0;
            if (okay) cfg.port = (int)value;
        }
        if (nick.size) okay = okay && copy_word(cfg.nick, sizeof cfg.nick, nick);
        if (!okay) { config_wipe(&cfg, sizeof cfg); return usage(c, command); }
        int result = client_add(c->client, &cfg, &index, error, sizeof error);
        config_wipe(&cfg, sizeof cfg);
        if (result < 0) { emit_fields(c, "Server not added: {detail}", (const IrcDisplayField[]){{"detail", error}}, 1); return IDLE_COMMAND_ERROR; }
        char number_text[32];
        (void)snprintf(number_text, sizeof number_text, "%zu", server_label(c, index));
        emit_fields(c, "Added server {number} with verified TLS and autoconnect off. /server {select} selects it; /connect then connects; /save persists.", (const IrcDisplayField[]){{"number", number_text}, {"select", number_text}}, 2);
        return IDLE_COMMAND_DONE;
    }
    if (equal(action, "connect") || equal(action, "disconnect") || equal(action, "close")) {
        if (!server_number(c, word(action.rest), &index)) return IDLE_COMMAND_ERROR;
        if (equal(action, "connect")) {
            if (client_connect(c->client, index, error, sizeof error) < 0) { emit(c, "%s", error); return IDLE_COMMAND_ERROR; }
        } else if (equal(action, "disconnect")) client_disconnect(c->client, index);
        else if (client_close(c->client, index, error, sizeof error) < 0) { emit(c, "%s", error); return IDLE_COMMAND_ERROR; }
        char number_text[32], action_text[16];
        (void)snprintf(number_text, sizeof number_text, "%zu", server_label(c, index));
        (void)snprintf(action_text, sizeof action_text, "%.*s", (int)action.size, action.text);
        emit_fields(c, "Server {number}: {action} requested.", (const IrcDisplayField[]){{"number", number_text}, {"action", action_text}}, 2);
        return IDLE_COMMAND_DONE;
    }
    if (!digits(action)) return connect_command(c, command, rest);
    if (!server_number(c, action, &index)) return usage(c, command);
    App *next = client_get_app(c->client, index, error, sizeof error);
    if (!next) { emit(c, "%s", error); return IDLE_COMMAND_ERROR; }
    if (!c->defer_visibility) app_set_visible(a, 0);
    c->client->current = index;
    c->client->default_unavailable = 0;
    if (!c->defer_visibility) app_set_visible(next, 1);
    char number_text[32];
    (void)snprintf(number_text, sizeof number_text, "%zu", server_label(c, index));
    emit_fields(c, "Selected server {number}: {host} | {status}", (const IrcDisplayField[]){{"number", number_text}, {"host", *next->cfg.server ? next->cfg.server : display_text(c, "(not configured)")}, {"status", app_connection_label(next)}}, 3);
    return IDLE_COMMAND_DONE;
}

static int save_one(IdleCommands *c, size_t index, int quiet)
{
    ClientSlot *slot = &c->client->slots[index];
    char error[512], number_text[32]; Config cfg;
    (void)snprintf(number_text, sizeof number_text, "%zu", server_label(c, index));
    if (slot->load_failed) { emit_fields(c, "Server {number} was not saved: profile could not be loaded.", (const IrcDisplayField[]){{"number", number_text}}, 1); return 0; }
    if (slot->app) app_config_effective(slot->app, &cfg); else cfg = slot->seed;
    int result = client_save(c->client, index, &cfg, error, sizeof error);
    config_wipe(&cfg, sizeof cfg);
    if (result < 0) { emit_fields(c, "Server {number} save did not complete: {error}", (const IrcDisplayField[]){{"number", number_text}, {"error", error}}, 2); return 0; }
    if (!quiet) emit_fields(c, "Saved server {number}{preferences}.", (const IrcDisplayField[]){{"number", number_text}, {"preferences", index == 0 ? display_text(c, " (including application preferences)") : ""}}, 2);
    return 1;
}

static int save(IdleCommands *c, const char *rest, const Command *command)
{
    Word w = word(rest);
    if (*w.rest || (w.size && !equal(w, "input") && !equal(w, "output") && !equal(w, "all"))) return usage(c, command);
    if (!w.size || equal(w, "input") || equal(w, "output"))
        return save_one(c, w.size ? 0 : c->client->current, 0) ? IDLE_COMMAND_DONE : IDLE_COMMAND_ERROR;
    size_t done = 0, failed = 0;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (c->client->slots[i].used) { if (save_one(c, i, 0)) ++done; else ++failed; }
    char done_text[32], failed_text[32];
    (void)snprintf(done_text, sizeof done_text, "%zu", done);
    (void)snprintf(failed_text, sizeof failed_text, "%zu", failed);
    emit_fields(c, "Saved {done} profile(s); {failed} failed. Each successful file remains saved.", (const IrcDisplayField[]){{"done", done_text}, {"failed", failed_text}}, 2);
    return failed ? IDLE_COMMAND_ERROR : IDLE_COMMAND_DONE;
}

static int saveconf(IdleCommands *c, const char *rest, const Command *command)
{
    if (*rest) return usage(c, command);
    if (c->client->test_mode) {
        emit(c, "%s", display_text(c, "OFFLINE TEST MODE: /saveconf does not write configuration."));
        return IDLE_COMMAND_ERROR;
    }
    size_t saved = 0, failed = 0;
    for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i)
        if (i != c->client->current && c->client->slots[i].used) {
            if (save_one(c, i, 1)) ++saved; else ++failed;
        }
    char error[512];
    int remembered = client_save_default(c->client, c->client->current, error, sizeof error) > 0;
    if (remembered) ++saved;
    else { ++failed; emit(c, "%s", error); }
    char saved_text[32], failed_text[32];
    (void)snprintf(saved_text, sizeof saved_text, "%zu", saved);
    (void)snprintf(failed_text, sizeof failed_text, "%zu", failed);
    if (failed) {
        emit_fields(c, "Configuration save incomplete: {saved} server(s) saved; {failed} failed. Selected default {state}. Successful writes remain saved.", (const IrcDisplayField[]){{"saved", saved_text}, {"failed", failed_text}, {"state", display_text(c, remembered ? "remembered" : "not confirmed")}}, 3);
        return IDLE_COMMAND_ERROR;
    }
    emit_fields(c, "Saved configuration for {count} server(s). /connect will reuse the selected connection next time.", (const IrcDisplayField[]){{"count", saved_text}}, 1);
    return IDLE_COMMAND_DONE;
}

/* Only validated names are echoed or saved. Actions use the primary owner's
 * current effective config, preserving staged connection/authentication data. */
static int hotkey_command(IdleCommands *c, const char *rest, const Command *command)
{
    Word first = word(rest), second = word(first.rest);
    const char *name = command->spec.name;
    char key_name[CONFIG_HOTKEY_NAME_SIZE], action_name[CONFIG_HOTKEY_NAME_SIZE], error[512];
    if (!strcmp(name, "bindings")) {
        if (*first.rest || (first.size && !equal(first, "keys") && !equal(first, "actions")))
            return usage(c, command);
        if (equal(first, "keys")) {
            emit(c, "%s", display_text(c, "Supported keys: F1..F12; Ctrl-A..Ctrl-Z; Alt-a..Alt-z; Alt-0..Alt-9."));
            emit(c, "Up Down Left Right Home End PageUp PageDown Insert Delete Backspace Tab Shift-Tab Enter Escape");
            emit(c, "%s", display_text(c, "Aliases: Ctrl-H = Backspace, Ctrl-I = Tab, Ctrl-J/Ctrl-M = Enter, Ctrl-[ = Escape."));
            emit(c, "%s", display_text(c, "Alt letters are unshifted lowercase. Terminal signals and terminal/multiplexer shortcuts take precedence."));
            return IDLE_COMMAND_DONE;
        }
        if (equal(first, "actions")) {
            emit(c, "%s", display_text(c, "Custom key actions (local program functions):"));
            for (size_t i = 0; i < config_hotkey_action_count(); ++i) {
                const ConfigHotkeyAction *action = config_hotkey_action_at(i);
                emit(c, "  %-12s %s", action->name, display_text(c, action->description));
            }
            emit(c, "%s", display_text(c, "Only main chat/command input accepts bindings; paste and modal screens retain their controls."));
            return IDLE_COMMAND_DONE;
        }
        if (!c->client || !c->client->slots[0].used) {
            emit(c, "%s", display_text(c, "No application configuration is available.")); return IDLE_COMMAND_ERROR;
        }
        const ClientSlot *primary = &c->client->slots[0];
        const Config *cfg = primary->app ? &primary->app->cfg : &primary->seed;
        if (cfg->hotkey_count > CONFIG_HOTKEY_MAX) {
            emit(c, "%s", display_text(c, "Invalid custom key binding table.")); return IDLE_COMMAND_ERROR;
        }
        char count_text[32];
        (void)snprintf(count_text, sizeof count_text, "%zu", cfg->hotkey_count);
        emit_fields(c, "Custom key bindings: {count} (application-wide; none assigned by default)", (const IrcDisplayField[]){{"count", count_text}}, 1);
        emit(c, "  %s | %s", display_text(c, "Key"), display_text(c, "Action"));
        for (size_t i = 0; i < cfg->hotkey_count; ++i) {
            const ConfigHotkeyAction *action = config_hotkey_action(cfg->hotkeys[i].action);
            if (action && config_hotkey_key_name(cfg->hotkeys[i].key, key_name, sizeof key_name))
                emit(c, "  %-12s %s", key_name, action->name);
        }
        emit(c, "%s", display_text(c, "/bind KEY ACTION assigns; /unbind KEY|all removes; /save input saves the list."));
        return IDLE_COMMAND_DONE;
    }
    int clear = !strcmp(name, "unbind") && equal(first, "all") && !*first.rest;
    unsigned key = 0;
    const ConfigHotkeyAction *action = NULL;
    if (!clear) {
        if (!copy_word(key_name, sizeof key_name, first) || !(key = config_hotkey_key(key_name)))
            return usage(c, command);
        if (!strcmp(name, "bind")) {
            if (*second.rest || !copy_word(action_name, sizeof action_name, second) ||
                !(action = config_hotkey_action_find(action_name))) return usage(c, command);
        } else if (*first.rest) return usage(c, command);
    }
    if (!c->client) { emit(c, "%s", display_text(c, "No application configuration is available.")); return IDLE_COMMAND_ERROR; }
    App *primary = client_get_app(c->client, 0, error, sizeof error);
    if (!primary) { emit(c, "%s", error); return IDLE_COMMAND_ERROR; }
    Config candidate; app_config_effective(primary, &candidate);
    if (clear) { candidate.hotkey_count = 0; memset(candidate.hotkeys, 0, sizeof candidate.hotkeys); }
    else if (config_hotkey_set(&candidate, key, action ? action->id : CONFIG_ACTION_NONE,
                              error, sizeof error) < 0) {
        config_wipe(&candidate, sizeof candidate);
        emit_fields(c, "Bindings unchanged: {error}", (const IrcDisplayField[]){{"error", display_config_error(c, error, (char[1024]){0}, 1024)}}, 1); return IDLE_COMMAND_ERROR;
    }
    int result = client_interface_apply(c->client, &candidate, 0, error, sizeof error);
    config_wipe(&candidate, sizeof candidate);
    if (result < 0) { emit_fields(c, "Bindings unchanged: {error}", (const IrcDisplayField[]){{"error", display_config_error(c, error, (char[1024]){0}, 1024)}}, 1); return IDLE_COMMAND_ERROR; }
    if (clear) emit(c, "%s", display_text(c, "All custom key bindings removed. /save input saves this preference."));
    else {
        (void)config_hotkey_key_name(key, key_name, sizeof key_name);
        if (action) emit_fields(c, "Bound {key} to {action} for all servers. /save input saves this preference.", (const IrcDisplayField[]){{"key", key_name}, {"action", action->name}}, 2);
        else emit_fields(c, "Removed {key} custom binding. /save input saves this preference.", (const IrcDisplayField[]){{"key", key_name}}, 1);
    }
    return IDLE_COMMAND_DONE;
}

static int safe_input(const char *text, size_t *length)
{
    if (!text) return 0;
    *length = strnlen(text, APP_TEXT + 1u);
    if (*length > APP_TEXT || !irc_utf8_valid(text)) return 0;
    for (size_t i = 0; i < *length; ++i)
        if (((unsigned char)text[i] < 32 && text[i] != '\n') || (unsigned char)text[i] == 127) return 0;
    return 1;
}

static int forward(IdleCommands *c, App *a, const Command *command, const char *rest, SendPlan *plan)
{
    char line[APP_TEXT + 1];
    const char *name = !strcmp(command->spec.name, "query") ? "pm" : command->spec.name;
    int n = snprintf(line, sizeof line, "/%s%s%s", name, *rest ? " " : "", rest);
    if (n < 0 || (size_t)n >= sizeof line) { emit(c, "%s", display_text(c, "Command exceeds the input byte limit.")); return IDLE_COMMAND_ERROR; }
    int result = app_command(a, line, plan);
    config_wipe(line, sizeof line);
    if (result == 2) return IDLE_COMMAND_SEND;
    if (result == 3) { list_channels(c, a); return IDLE_COMMAND_DONE; }
    if (result == 4) { list_queries(c, a); return IDLE_COMMAND_DONE; }
    if (result == 5) { overview(c); return IDLE_COMMAND_DONE; }
    if (result == 6) {
        if (!strcmp(command->spec.name, "list") && !strncmp(rest, "show", 4) &&
            (!rest[4] || rest[4] == ' ')) list_network_channels(c, a);
        return IDLE_COMMAND_DONE;
    }
    return result ? IDLE_COMMAND_DONE : IDLE_COMMAND_ERROR;
}

/* Dispatch validates the requested action against the current slot. A SEND
 * result supplies a plan for the frontend to review; it is not permission to
 * bypass the frontend's paste, target-identity or confirmation guards. */
static int idle_command_run(IdleCommands *c, const char *text, SendPlan *plan)
{
    size_t length;
    if (plan) config_wipe(plan, sizeof *plan);
    if (!c || !safe_input(text, &length) || !length || *text != '/' || text[1] == '/') {
        emit(c, "%s", display_text(c, "Enter a supported /command; /help lists commands. Invalid controls or oversized input are rejected."));
        return IDLE_COMMAND_ERROR;
    }
    Word name = word(text + 1);
    const Command *command = find_command(name);
    if (!command) { emit(c, "%s", display_text(c, "Unknown command. Use /help commands or /help COMMAND; shell and raw IRC commands are unavailable.")); return IDLE_COMMAND_ERROR; }
    const char *rest = name.rest;
    const char *key = command->spec.name;
    if (strchr(text, '\n') && strcmp(key, "msg") && strcmp(key, "me")) return usage(c, command);
    if (!strcmp(key, "help")) return help(c, rest, command);
    if (!strcmp(key, "bind") || !strcmp(key, "unbind") || !strcmp(key, "bindings"))
        return hotkey_command(c, rest, command);
    if (!strcmp(key, "manual")) { if (*rest) return usage(c, command); manual_to(c); return IDLE_COMMAND_DONE; }
    if (!strcmp(key, "summary")) {
        if (*rest) return usage(c, command);
        summary_print(c->client, c->dcc, c->server_base, c->print, c->context);
        return IDLE_COMMAND_DONE;
    }
    if (!strcmp(key, "panel") || !strcmp(key, "quickstart")) {
        if (*rest) return usage(c, command);
        return !strcmp(key, "panel") ? IDLE_COMMAND_PANEL : IDLE_COMMAND_QUICKSTART;
    }
    if (!strcmp(key, "mentions") && *rest) {
        Word action = word(rest);
        if (!equal(action, "clear") || *action.rest) return usage(c, command);
        for (size_t i = 0; i < CLIENT_MAX_SERVERS; ++i) {
            ClientSlot *slot = &c->client->slots[i];
            if (slot->used && slot->app) (void)app_clear_mentions(slot->app);
        }
        emit(c, "%s", display_text(c, "Cleared retained mentions across all servers."));
        return IDLE_COMMAND_MENTIONS_CLEAR;
    }
    App *a = selected(c);
    if (!a) return IDLE_COMMAND_ERROR;
    if (!strcmp(key, "dcc") || !strcmp(key, "transfers")) return IDLE_COMMAND_DCC;
    if (!strcmp(key, "connect")) return connect_command(c, command, rest);
    if (!strcmp(key, "server")) return server_command(c, a, command, rest);
    if (!strcmp(key, "save")) return save(c, rest, command);
    if (!strcmp(key, "saveconf")) return saveconf(c, rest, command);
    if (!strcmp(key, "autoconnect") || !strcmp(key, "autojoin") || !strcmp(key, "autoauth")) return startup_command(c, a, command, rest);
    if (!strcmp(key, "autocommands")) return autocommands_command(c, a, command, rest);
    if (!strcmp(key, "users")) return roster(c, a, rest, command);
    if (!strcmp(key, "set")) {
        Word setting = word(rest), value = word(setting.rest);
        const char *text_value = setting.rest;
        int send = equal(setting, "send_mode"), composer = equal(setting, "composer_mode"), output = equal(setting, "output_format"),
            queue = equal(setting, "queue_notices"), program_messages = equal(setting, "program_messages"),
            persistent_logging = equal(setting, "persistent_logging"), single_session = equal(setting, "single_session"),
            default_interface = equal(setting, "default_interface"), suggestions = equal(setting, "command_suggestions"),
            part_message = equal(setting, "part_message"), quit_message = equal(setting, "quit_message"),
            background_away = equal(setting, "background_away"), background_message = equal(setting, "background_away_message"),
            auto_minutes = equal(setting, "autoaway_minutes"), auto_message = equal(setting, "autoaway_message"), mode = -1;
        int presence = part_message || quit_message || background_away || background_message || auto_minutes || auto_message;
        if (suggestions) for (int i = CONFIG_SUGGEST_ALPHABETICAL; i <= CONFIG_SUGGEST_CONTEXTUAL; ++i)
            if (equal(value, commands_suggestion_mode_name(i))) mode = i;
        int minutes = -1;
        if (auto_minutes && value.size && !*value.rest) {
            char *end = NULL;
            errno = 0;
            long parsed = strtol(text_value, &end, 10);
            if (!errno && end && !*end && parsed >= 0 && parsed <= 1440) minutes = (int)parsed;
        }
        if ((!send && !composer && !output && !queue && !program_messages && !persistent_logging && !single_session &&
             !default_interface && !suggestions && !presence) ||
            ((!part_message && !quit_message && !background_message && !auto_message) && *value.rest) ||
            (send && (!value.size || (!equal(value, "direct") && !equal(value, "review")))) ||
            (composer && (!value.size || (!equal(value, "line") && !equal(value, "fullscreen")))) ||
            (output && (!value.size || (!equal(value, "compact") && !equal(value, "full")))) ||
            (default_interface && (!value.size || (!equal(value, "idle") && !equal(value, "ncurses")))) ||
            ((queue || program_messages || persistent_logging || single_session || background_away) &&
             (!value.size || (!equal(value, "on") && !equal(value, "off")))) ||
            (suggestions && mode < 0) || (auto_minutes && minutes < 0) ||
            ((background_message || auto_message) && (!*text_value || strlen(text_value) >= 256)) ||
            ((part_message || quit_message) && strlen(text_value) >= 256)) return usage(c, command);
        char error[512]; App *primary = client_get_app(c->client, 0, error, sizeof error);
        if (!primary) { emit(c, "%s", error); return IDLE_COMMAND_ERROR; }
        Config cfg; app_config_effective(primary, &cfg);
        if (default_interface && cfg.minimal && equal(value, "ncurses")) {
            config_wipe(&cfg, sizeof cfg);
            emit(c, "%s", display_text(c, "onekeyirc always uses the idle interface; ncurses requires vipirc. The saved preference is unchanged."));
            return IDLE_COMMAND_ERROR;
        }
        if (queue || program_messages || persistent_logging || single_session || default_interface || suggestions || composer || presence) {
            if (queue) cfg.queue_notices = equal(value, "on");
            else if (program_messages) cfg.program_messages = equal(value, "on");
            else if (persistent_logging) cfg.persistent_logging = equal(value, "on");
            else if (single_session) cfg.single_session = equal(value, "on");
            else if (default_interface) cfg.default_interface = equal(value, "idle") ? CONFIG_INTERFACE_IDLE : CONFIG_INTERFACE_NCURSES;
            else if (suggestions) cfg.command_suggestions = mode;
            else if (composer) cfg.composer_fullscreen = equal(value, "fullscreen");
            else if (part_message) (void)snprintf(cfg.part_message, sizeof cfg.part_message, "%s", text_value);
            else if (quit_message) (void)snprintf(cfg.quit_message, sizeof cfg.quit_message, "%s", text_value);
            else if (background_away) cfg.background_away = equal(value, "on");
            else if (background_message) (void)snprintf(cfg.background_away_message, sizeof cfg.background_away_message, "%s", text_value);
            else if (auto_minutes) cfg.autoaway_minutes = minutes;
            else if (auto_message) (void)snprintf(cfg.autoaway_message, sizeof cfg.autoaway_message, "%s", text_value);
            int okay = client_interface_apply(c->client, &cfg, 0, error, sizeof error);
            config_wipe(&cfg, sizeof cfg);
            if (okay < 0) { emit_fields(c, "Settings unchanged: {detail}", (const IrcDisplayField[]){{"detail", display_config_error(c, error, (char[1024]){0}, 1024)}}, 1); return IDLE_COMMAND_ERROR; }
            emit(c, "%s", queue ? "Queue notices changed for all servers. /save output saves application preferences." :
                 program_messages ? "Program messages changed for all servers. /save output saves application preferences." :
                 persistent_logging ? "Persistent logging changed for all servers. /save output saves application preferences." :
                 single_session ? "Single-session mode changed for all servers. /save output saves application preferences." :
                 default_interface ? "Default interface changed for the next fresh launch. /save input saves this preference." :
                 suggestions ? "Command suggestions changed for all servers. /save input saves application preferences." :
                 composer ? "Composer mode changed for all servers. /save input saves application preferences." :
                 "Presence preference changed for all servers. /save input saves it.");
            return IDLE_COMMAND_DONE;
        }
        if (send) cfg.send_review = equal(value, "review");
        else cfg.output_full = equal(value, "full");
        return apply(c, primary, &cfg, send ?
                     "Send mode changed for all servers. /save input saves application preferences." :
                     "Output format changed for all servers. /save output saves application preferences.");
    }
    if (!strcmp(key, "disconnect")) {
        size_t index = c->client->current;
        if (*rest && !server_number(c, word(rest), &index)) return IDLE_COMMAND_ERROR;
        client_disconnect(c->client, index);
        char number_text[32];
        (void)snprintf(number_text, sizeof number_text, "%zu", server_label(c, index));
        emit_fields(c, "Server {number}: {action} requested.", (const IrcDisplayField[]){{"number", number_text}, {"action", key}}, 2); return IDLE_COMMAND_DONE;
    }
    if (!strcmp(key, "ls")) {
        Word which = word(rest);
        if (*which.rest) return usage(c, command);
        if (!which.size || equal(which, "channels")) list_channels(c, a);
        else if (equal(which, "servers")) list_servers(c);
        else if (equal(which, "queries")) list_queries(c, a);
        else if (equal(which, "unread")) list_unread(c);
        else return usage(c, command);
        return IDLE_COMMAND_DONE;
    }
    if (!strcmp(key, "quit")) {
        if (strlen(rest) >= 256 || !client_request_quit(c->client, rest)) {
            emit(c, "%s", *app_status_text(a) ? app_status_text(a) : "QUIT reason could not be sent.");
            return IDLE_COMMAND_ERROR;
        }
        return IDLE_COMMAND_QUIT;
    }
    if (!strcmp(key, "servers") || !strcmp(key, "sl") || !strcmp(key, "channels") || !strcmp(key, "cl") || !strcmp(key, "views") ||
        !strcmp(key, "ql") || !strcmp(key, "queries") || !strcmp(key, "unread") || !strcmp(key, "settings") ||
        !strcmp(key, "background") || !strcmp(key, "clear") ||
        !strcmp(key, "mentions") || !strcmp(key, "next") || !strcmp(key, "prev") ||
        !strcmp(key, "back") || !strcmp(key, "cancel")) {
        if (*rest) return usage(c, command);
        if (!strcmp(key, "servers") || !strcmp(key, "sl")) list_servers(c);
        else if (!strcmp(key, "channels") || !strcmp(key, "cl") || !strcmp(key, "views")) list_channels(c, a);
        else if (!strcmp(key, "ql") || !strcmp(key, "queries")) list_queries(c, a);
        else if (!strcmp(key, "unread")) list_unread(c);
        else if (!strcmp(key, "settings")) return settings(c, a);
        else if (!strcmp(key, "background")) return IDLE_COMMAND_BACKGROUND;
        else if (!strcmp(key, "clear")) return IDLE_COMMAND_CLEAR;
        else return forward(c, a, command, rest, plan);
        return IDLE_COMMAND_DONE;
    }
    if (!strcmp(key, "query") || !strcmp(key, "pm")) {
        Word target = word(rest); char nick[64];
        if (*target.rest || !copy_word(nick, sizeof nick, target) ||
            (!digits(target) && !irc_valid_nick(nick))) return usage(c, command);
        return forward(c, a, command, nick, plan);
    }
    if (!strcmp(key, "channel") || !strcmp(key, "view") || !strcmp(key, "pm") ||
        !strcmp(key, "nick") || !strcmp(key, "whois") || !strcmp(key, "unignore")) {
        if (count_words(rest) != 1) return usage(c, command);
    } else if (!strcmp(key, "close")) {
        if (count_words(rest) > 1) return usage(c, command);
    } else if (!strcmp(key, "part")) {
        /* Optional channel plus free-form reason; app_command validates both. */
    } else if (!strcmp(key, "list")) {
        /* app_command validates the richer LIST grammar shared with curses. */
    } else if (!strcmp(key, "join")) {
        size_t count = count_words(rest); if (count < 1 || count > 2) return usage(c, command);
    } else if (!strcmp(key, "msg")) {
        Word target = word(rest); if (!target.size || !*target.rest || !plan) return usage(c, command);
    } else if (!strcmp(key, "me")) {
        if (!*rest || !plan) return usage(c, command);
    } else if (!strcmp(key, "watch")) {
        Word action = word(rest);
        if (!action.size || (equal(action, "list") && !*action.rest)) {
            char count_text[32];
            (void)snprintf(count_text, sizeof count_text, "%zu", a->cfg.word_count);
            emit_fields(c, "Watch strings: nickname {nick} plus {count} literal terms", (const IrcDisplayField[]){{"nick", a->nick}, {"count", count_text}}, 2);
            for (size_t i = 0; i < a->cfg.word_count; ++i) emit(c, "  %s", a->cfg.words[i]);
            return IDLE_COMMAND_DONE;
        }
        if ((!equal(action, "add") && !equal(action, "remove")) || !*action.rest) return usage(c, command);
    } else if (!strcmp(key, "ignore")) {
        Word action = word(rest);
        if (!action.size || (equal(action, "list") && !*action.rest)) {
            char count_text[32];
            (void)snprintf(count_text, sizeof count_text, "%zu", a->cfg.ignore_count);
            emit_fields(c, "Ignore masks: {count}", (const IrcDisplayField[]){{"count", count_text}}, 1);
            for (size_t i = 0; i < a->cfg.ignore_count; ++i) emit(c, "  %s", a->cfg.ignores[i]);
            return IDLE_COMMAND_DONE;
        }
        if (equal(action, "list") || (equal(action, "clear") && *action.rest) ||
            ((equal(action, "add") || equal(action, "remove")) ? count_words(action.rest) != 1 : *action.rest != 0)) return usage(c, command);
    }
    return forward(c, a, command, rest, plan);
}

/* Alias expansion happens before dispatch, but is
 * bounded to one local command. Always wipe the expansion buffer; command
 * text may carry secrets even when recording/history is disabled. */
int idle_command(IdleCommands *c, const char *text, SendPlan *plan)
{
    char expanded[APP_TEXT + 1];
    int changed = commands_expand_alias(text, expanded, sizeof expanded);
    int result;
    if (changed < 0) {
        if (plan) config_wipe(plan, sizeof *plan);
        emit(c, "%s", display_text(c, "Command exceeds the input byte limit.")); result = IDLE_COMMAND_ERROR;
    } else result = idle_command_run(c, changed ? expanded : text, plan);
    config_wipe(expanded, sizeof expanded);
    return result;
}

/* Recall is a second consumer of command text. It must independently reject
 * sensitive forms even when execution would refuse the command elsewhere. */
static int recordable(const char *text)
{
    size_t length;
    if (!safe_input(text, &length) || !length || *text != '/' || strchr(text, '\n')) return 0;
    Word name = word(text + 1);
    const Command *command = find_command(name);
    if (!command || equal(name, "autoauth") || equal(name, "autocommands") ||
        equal(name, "znc") || equal(name, "soju")) return 0;
    if (equal(name, "connect") || equal(name, "server")) {
        Word first = word(name.rest);
        int management = equal(name, "server") &&
                         (equal(first, "add") || equal(first, "connect") ||
                          equal(first, "disconnect") || equal(first, "close"));
        if (!management && first.size) {
            if (digits(first)) {
                size_t index;
                /* Execution validates each frontend's numbering. The history
                 * filter accepts either base and never interprets a secret. */
                if (*first.rest || !number(first, CLIENT_MAX_SERVERS, &index)) return 0;
            } else {
                ConnectRequest request;
                int valid = connect_parse(NULL, command, name.rest, &request);
                config_wipe(&request, sizeof request);
                if (!valid) return 0;
            }
        }
    }
    if (equal(name, "join") && *word(name.rest).rest) return 0;
    if (equal(name, "msg")) {
        Word target = word(name.rest);
        /* Match the core authentication guard's leading colon/space rules,
         * including standalone colons before the credential verb. */
        const char *body = target.rest;
        while (*body == ' ' || *body == ':') ++body;
        Word first = word(body);
        static const char *const services[] = {"nickserv", "authserv", "chanserv", "operserv", "hostserv", "memoserv", "groupserv", "bouncerserv", "ns", "as", "cs", "os", "q", "x"};
        static const char *const secrets[] = {"identify", "id", "auth", "authenticate", "login", "password", "pass", "register", "ghost", "recover", "release", "group", "drop", "resetpass", "setpass", "changepass", "verify", "oper", "set", "saset"};
        if (target.size && target.text[0] == '*') return 0;
        for (size_t i = 0; i < target.size; ++i) if (target.text[i] == '@') { target.size = i; break; }
        for (size_t i = 0; i < sizeof services / sizeof services[0]; ++i) if (equal(target, services[i])) return 0;
        for (size_t i = 0; i < sizeof secrets / sizeof secrets[0]; ++i) if (equal(first, secrets[i])) return 0;
    }
    char safe[APP_TEXT + 1];
    size_t size = irc_sanitize(safe, sizeof safe, text);
    int okay = size == length && !memcmp(safe, text, length);
    config_wipe(safe, sizeof safe);
    return okay;
}

int idle_command_recordable(const char *text)
{
    char expanded[APP_TEXT + 1];
    int changed = commands_expand_alias(text, expanded, sizeof expanded);
    int result = changed < 0 ? 0 : recordable(changed ? expanded : text);
    config_wipe(expanded, sizeof expanded);
    return result;
}
