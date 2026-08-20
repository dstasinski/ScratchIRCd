/**
 * @file dispatch.c
 * @brief Maps IRC command names to command-specific handler functions.
 */

#include "commands.h"
#include "numerics.h"

#include <stddef.h>
#include <strings.h>

typedef struct CommandEntry {
    const char *name;
    CommandHandler handler;
} CommandEntry;

static const CommandEntry command_table[] = {
    {"ADMIN", command_admin}, {"AWAY", command_away}, {"INVITE", command_invite},
    {"ISON", command_ison}, {"JOIN", command_join}, {"KICK", command_kick},
    {"KILL", command_kill}, {"KLINE", command_kline}, {"LIST", command_list},
    {"LUSERS", command_lusers}, {"MODE", command_mode}, {"MOTD", command_motd},
    {"NAMES", command_names}, {"NICK", command_nick}, {"NOTICE", command_notice},
    {"OPER", command_oper}, {"OPERADD", command_operadd}, {"OPERDEL", command_operdel},
    {"OPERLIST", command_operlist}, {"OPERSET", command_operset}, {"PART", command_part},
    {"PASS", command_pass}, {"PING", command_ping}, {"PRIVMSG", command_privmsg},
    {"QUIT", command_quit}, {"REHASH", command_rehash}, {"RESTART", command_restart},
    {"RULES", command_rules}, {"SAJOIN", command_sajoin}, {"SAMODE", command_samode},
    {"SAPART", command_sapart}, {"SETHOST", command_sethost},
    {"SETIDENT", command_setident}, {"SETNAME", command_setname},
    {"TOPIC", command_topic}, {"USER", command_user}, {"USERHOST", command_userhost},
    {"USERIP", command_userip}, {"WALLOPS", command_wallops}, {"WHO", command_who},
    {"WHOIS", command_whois}, {"ZLINE", command_zline}
};

CommandResult command_dispatch(Server *server, Client *client,
                               const char *command, char *params) {
    size_t index;

    if (server == NULL || client == NULL || command == NULL) return COMMAND_KEEP_CLIENT;
    for (index = 0U; index < sizeof(command_table) / sizeof(command_table[0]); ++index) {
        if (strcasecmp(command, command_table[index].name) == 0)
            return command_table[index].handler(server, client, params);
    }
    client_sendf(client, ERR_UNKNOWNCOMMAND,
                 server->config.server_name, command_reply_nick(client), command);
    return COMMAND_KEEP_CLIENT;
}
