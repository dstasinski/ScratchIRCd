/**
 * @file dispatch.c
 * @brief Maps IRC command names to command-specific handler functions.
 *
 * The dispatcher deliberately contains no implementation details for NICK,
 * USER, JOIN, PRIVMSG, or any other IRC command.  Adding a new command requires
 * implementing its handler in src/commands/ and adding one table entry here.
 *
 * Command names are matched case-insensitively as required by typical IRC
 * client behavior.  Unknown commands use ERR_UNKNOWNCOMMAND from numerics.h.
 */

#include "commands.h"
#include "config.h"
#include "numerics.h"

#include <stddef.h>
#include <strings.h>

/** One immutable command-name to function mapping. */
typedef struct CommandEntry {
    const char *name;          /**< IRC command token, e.g. "PRIVMSG". */
    CommandHandler handler;    /**< Function implementing the command. */
} CommandEntry;

/** Commands supported by this iteration of the server. */
static const CommandEntry command_table[] = {
    {"NICK",    command_nick},
    {"USER",    command_user},
    {"PING",    command_ping},
    {"JOIN",    command_join},
    {"PART",    command_part},
    {"PRIVMSG", command_privmsg},
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
                 IRCD_SERVER_NAME, command_reply_nick(client), command);
    return COMMAND_KEEP_CLIENT;
}
