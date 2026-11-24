/* Locale-selection and literal-protocol regression. No network or credentials. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IRC_L10N_IMPLEMENTATION
#include "l10n.h"

static void check_language(const char *locale, const char *help,
                           const char *settings, const char *password,
                           const char *current, const char *choice)
{
    assert(setenv("VIPIRC_LANG", locale, 1) == 0);
    assert(strcmp(irc_tr("Help"), help) == 0);
    assert(strcmp(irc_tr("Settings"), settings) == 0);
    assert(strcmp(irc_tr("Server password"), password) == 0);
    assert(strcmp(irc_tr("Current"), current) == 0);
    assert(strcmp(irc_tr("Choice:"), choice) == 0);
    assert(strcmp(irc_tr("/join"), "/join") == 0);
    if (strcmp(help, "Help") != 0) {
        assert(strcmp(irc_tr("Channel"), "Channel") != 0);
        assert(strcmp(irc_tr("No matching command history."), "No matching command history.") != 0);
    }
    assert(strstr(irc_tr("No channels match this LIST."), "LIST") != NULL);
    assert(strstr(irc_tr("Enter: choose"), "Enter") != NULL);

    assert(strcmp(irc_tr("/msg"), "/msg") == 0);
    assert(strcmp(irc_tr("sasl"), "sasl") == 0);
    assert(strcmp(irc_tr("direct"), "direct") == 0);
    assert(strcmp(irc_tr("unknown text"), "unknown text") == 0);
    assert(irc_tr(NULL) == NULL);
}

int main(void)
{
    assert(setenv("LC_ALL", "C", 1) == 0);
    assert(setenv("LC_MESSAGES", "C", 1) == 0);
    assert(setenv("LANG", "C", 1) == 0);
    assert(unsetenv("ONEKEYIRC_LANG") == 0);
    check_language("id_ID.UTF-8", "Bantuan", "Pengaturan", "Sandi server", "Saat ini", "Pilihan:");
    check_language("ms_MY.UTF-8", "Bantuan", "Tetapan", "Kata laluan pelayan", "Semasa", "Pilihan:");
    check_language("tr_TR.UTF-8", "Yardım", "Ayarlar", "Sunucu parolası", "Mevcut", "Seçim:");
    check_language("es_ES.UTF-8", "Ayuda", "Ajustes", "Contraseña del servidor", "Actual", "Elección:");
    check_language("pt_BR.UTF-8", "Ajuda", "Configurações", "Senha do servidor", "Atual", "Escolha:");
    check_language("fr_FR.UTF-8", "Aide", "Paramètres", "Mot de passe serveur", "Actuel", "Choix :");
    check_language("de_DE.UTF-8", "Hilfe", "Einstellungen", "Serverpasswort", "Aktuell", "Auswahl:");
    check_language("ru_RU.UTF-8", "Помощь", "Настройки", "Пароль сервера", "Текущее", "Выбор:");
    check_language("it_IT.UTF-8", "Aiuto", "Impostazioni", "Password server", "Attuale", "Scelta:");
    check_language("ar_EG.UTF-8", "مساعدة", "الإعدادات", "كلمة مرور الخادم", "الحالي", "الاختيار:");

    check_language("C", "Help", "Settings", "Server password", "Current", "Choice:");
    check_language("en_US.UTF-8", "Help", "Settings", "Server password", "Current", "Choice:");
    check_language("trick", "Help", "Settings", "Server password", "Current", "Choice:");
    assert(setenv("VIPIRC_LANG", "tr-TR", 1) == 0);
    assert(strcmp(irc_tr("Help"), "Yardım") == 0);
    assert(setenv("VIPIRC_LANG", "pt_BR.UTF-8@special", 1) == 0);
    assert(strcmp(irc_tr("Settings"), "Configurações") == 0);
    assert(unsetenv("VIPIRC_LANG") == 0);
    assert(setenv("ONEKEYIRC_LANG", "de_DE.UTF-8", 1) == 0);
    assert(strcmp(irc_tr("Settings"), "Einstellungen") == 0);
    assert(setenv("VIPIRC_LANG", "id_ID.UTF-8", 1) == 0);
    assert(strcmp(irc_tr("Settings"), "Pengaturan") == 0);
    assert(unsetenv("VIPIRC_LANG") == 0);
    assert(unsetenv("ONEKEYIRC_LANG") == 0);
    assert(setenv("LC_ALL", "fr_FR.UTF-8", 1) == 0);
    assert(strcmp(irc_tr("Settings"), "Paramètres") == 0);
    assert(unsetenv("LC_ALL") == 0);
    assert(setenv("LC_MESSAGES", "it_IT.UTF-8", 1) == 0);
    assert(strcmp(irc_tr("Help"), "Aiuto") == 0);
    assert(unsetenv("LC_MESSAGES") == 0);
    assert(setenv("LANG", "ms_MY.UTF-8", 1) == 0);
    assert(strcmp(irc_tr("Settings"), "Tetapan") == 0);
    assert(unsetenv("LANG") == 0);
    assert(strcmp(irc_tr("Help"), "Help") == 0);
    puts("localization: 10 languages, expanded labels, control tokens and fallback PASS");
    return 0;
}
