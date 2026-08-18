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
    {"NICK",    command_nick},
    {"USER",    command_user},
    {"PING",    command_ping},
    {"JOIN",    command_join},
    {"PART",    command_part},
    {"INVITE",  command_invite},
    {"KICK",    command_kick},
    {"LIST",    command_list},
    {"MODE",    command_mode},
    {"NAMES",   command_names},
    {"NOTICE",  command_notice},
    {"PRIVMSG", command_privmsg},
    {"TOPIC",   command_topic},
    {"WHO",     command_who},
    {"WHOIS",   command_whois},
    {"QUIT",    command_quit}
};

CommandResult command_dispatch(Server *server, Client *client,
                               const char *command, char *params) {
    size_t index;

    if (server == NULL || client == NULL || command == NULL) {
        return COMMAND_KEEP_CLIENT;
    }

    for (index = 0U; index < sizeof(command_table) / sizeof(command_table[0]);
         ++index) {
        if (strcasecmp(command, command_table[index].name) == 0) {
            return command_table[index].handler(server, client, params);
        }
    }

    client_sendf(client, ERR_UNKNOWNCOMMAND,
                 server->config.server_name, command_reply_nick(client), command);
    return COMMAND_KEEP_CLIENT;
}
