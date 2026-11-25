#define IRC_L10N_IMPLEMENTATION
#include "../src/transfer_notice_l10n.h"
#include <assert.h>
#include <stdlib.h>
int main(void) {
    const int english = !strcmp(getenv("VIPIRC_LANG"), "en");
    char out[4096], result[4096], tiny[2], raw[512];
    const char *reasons[] = {
        "Cannot display the DCC IPv4 address.",
        "Cannot secure DCC destination permissions.",
        "Choose a private receive directory.",
        "Choose a visible safe filename without paths or controls.",
        "DCC active-transfer limit is 4; finish or cancel one first.",
        "DCC destination is not a new regular file.",
        "DCC destination path is too long.",
        "DCC directory must be bounded UTF-8 without terminal controls.",
        "DCC filename has an unmatched quote.",
        "DCC filename is empty or too long.",
        "DCC filename must be a visible safe basename without paths or controls.",
        "DCC filename must be followed by an address.",
        "DCC offer does not fit the command buffer.",
        "DCC offer limit reached; reject an offer before accepting more.",
        "DCC paths cannot contain '..' components.",
        "DCC receive directory must be owned by you with no group/other permissions.",
        "DCC requires a decimal IPv4 address, port 1..65535 and size 0..4294967295; extensions are unsupported.",
        "DCC send requires a nickname, source path and explicit unicast IPv4 address.",
        "DCC source changed before opening. Select the file again.",
        "DCC source must be a regular file no larger than 4294967295 bytes.",
        "DCC source needs a visible safe basename without quotes or controls.",
        "DCC transfer list is full.",
        "Invalid DCC sender.",
        "OFFLINE TEST MODE: DCC receive is disabled.",
        "OFFLINE TEST MODE: DCC send is disabled.",
        "Only active DCC SEND with filename, IPv4, port and size is supported.",
        "Select a pending DCC offer to accept.",
        "Select a pending offer to reject.",
        "That DCC item is no longer active.",
        "Cancelled by user.",
        "DCC acknowledgement could not be sent.",
        "DCC destination close failed.",
        "DCC peer closed before the advertised file size arrived.",
        "DCC peer sent more than the advertised size.",
        "DCC destination write failed.",
        "DCC peer closed before acknowledging the complete file.",
        "DCC peer sent an invalid byte acknowledgement.",
        "DCC source ended or became unreadable before its advertised size.",
        "DCC source could not be sent to the peer.",
        "DCC socket failed.",
        "Cannot configure DCC peer socket.",
        "Cannot accept DCC peer connection.",
        "Cannot connect to the offered DCC endpoint.",
        "DCC transfer timed out without progress.",
        "DCC offer expired without acceptance.",
    };
    for (size_t i=0; i<sizeof reasons/sizeof reasons[0]; ++i) {
        const char *shown = irc_security_diagnostic(reasons[i], out, sizeof out);
        assert((strcmp(shown, reasons[i]) == 0) == english);
        assert(irc_transfer_result_notice(result, sizeof result, 99, "failed", "file%n{raw}.txt", reasons[i]));
        assert(strstr(result, shown) && strstr(result, "file%n{raw}.txt") && strstr(result,"99"));
    }
    const char *prefixes[] = {
        "Cannot open the DCC directory base: ",
        "Cannot open DCC directory (symlinks refused): ",
        "Cannot create a new DCC file (no overwrite): ",
        "Cannot open DCC connection: ",
        "Cannot connect to offered DCC endpoint: ",
        "Cannot open DCC source (symlinks refused): ",
        "Cannot create DCC listener: ",
        "Cannot bind/listen on the DCC port: ",
    };
    const char *detail = "OS detail %n {path}: 123";
    for (size_t i=0; i<sizeof prefixes/sizeof prefixes[0]; ++i) {
        snprintf(raw, sizeof raw, "%s%s", prefixes[i], detail);
        const char *shown=irc_security_diagnostic(raw,out,sizeof out);
        assert((strcmp(shown,raw)==0)==english && strstr(shown,detail));
        assert(irc_security_diagnostic(raw,tiny,sizeof tiny)==raw);
        assert(irc_transfer_result_notice(result,sizeof result,99,"failed","file%n{raw}.txt",raw));
        assert(strstr(result,shown) && strstr(result,"file%n{raw}.txt"));
        assert(irc_security_diagnostic(prefixes[i],out,sizeof out)==prefixes[i]);
    }
    assert(!strcmp(irc_security_diagnostic("unknown DCC error %n {x}",out,sizeof out),"unknown DCC error %n {x}"));
    puts("DCC retained reasons, system errors and result formatting: PASS");
    return 0;
}
