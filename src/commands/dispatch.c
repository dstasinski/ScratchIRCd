/** @file dispatch.c @brief Maps IRC command names to command handlers. */
#include "commands.h"
#include "numerics.h"
#include <stddef.h>
#include <strings.h>

typedef struct CommandEntry { const char *name; CommandHandler handler; } CommandEntry;

static const CommandEntry command_table[] = {
    {"ADMIN", command_admin}, {"AUTHENTICATE", command_authenticate}, {"AWAY", command_away},
    {"CAP", command_cap}, {"CHANSERV", command_chanserv}, {"CHATHISTORY", command_chathistory},
    {"CSDROP", command_csdrop}, {"CSINFO", command_csinfo}, {"CSSET", command_csset},
    {"DEAF", command_deaf}, {"GEOBAN", command_geoban}, {"GLOBOPS", command_globops},
    {"IDENTIFY", command_identify}, {"INVITE", command_invite}, {"ISON", command_ison},
    {"JOIN", command_join}, {"KICK", command_kick}, {"KILL", command_kill},
    {"KLINE", command_kline}, {"KNOCK", command_knock}, {"LIST", command_list},
    {"LOCOPS", command_locops}, {"LUSERS", command_lusers},
    {"MEMOSERV", command_memoserv}, {"MODE", command_mode}, {"MOTD", command_motd},
    {"MSINFO", command_msinfo}, {"MSPURGE", command_mspurge}, {"MUTE", command_mute},
    {"NAMES", command_names}, {"NICK", command_nick}, {"NICKSERV", command_nickserv},
    {"NOTICE", command_notice}, {"NSDROP", command_nsdrop}, {"NSINFO", command_nsinfo},
    {"NSSET", command_nsset}, {"OPER", command_oper}, {"OPERADD", command_operadd},
    {"OPERDEL", command_operdel}, {"OPERLIST", command_operlist}, {"OPERSET", command_operset},
    {"PART", command_part}, {"PASS", command_pass}, {"PING", command_ping},
    {"PRIVMSG", command_privmsg}, {"QUIT", command_quit}, {"REHASH", command_rehash},
    {"RESTART", command_restart}, {"RULES", command_rules}, {"SAJOIN", command_sajoin},
    {"SAMODE", command_samode}, {"SAPART", command_sapart}, {"SETHOST", command_sethost},
    {"SETIDENT", command_setident}, {"SETNAME", command_setname}, {"SILENCE", command_silence},
    {"TOPIC", command_topic}, {"UNGEOBAN", command_ungeoban}, {"USER", command_user},
    {"USERHOST", command_userhost}, {"USERIP", command_userip}, {"WALLOPS", command_wallops},
    {"WATCH", command_watch}, {"WEBIRC", command_webirc}, {"WHO", command_who},
    {"WHOIS", command_whois}, {"WHOWAS", command_whowas}, {"ZLINE", command_zline}
};

CommandResult command_dispatch(Server *server, Client *client,
                               const char *command, char *params) {
    size_t index;
    if (server == NULL || client == NULL || command == NULL) return COMMAND_KEEP_CLIENT;
    for (index = 0U; index < sizeof(command_table) / sizeof(command_table[0]); ++index)
        if (strcasecmp(command, command_table[index].name) == 0)
            return command_table[index].handler(server, client, params);
    client_sendf(client, ERR_UNKNOWNCOMMAND,
                 server->config.server_name, command_reply_nick(client), command);
    return COMMAND_KEEP_CLIENT;
}
