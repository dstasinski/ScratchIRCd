#ifndef IRCD_COMMANDS_H
#define IRCD_COMMANDS_H

/* Public interface for modular IRC command dispatch. */

#include "server.h"

typedef enum CommandResult {
    COMMAND_KEEP_CLIENT = 0,
    COMMAND_DISCONNECT_CLIENT = 1
} CommandResult;

typedef CommandResult (*CommandHandler)(Server *server, Client *client,
                                        char *params);

CommandResult command_dispatch(Server *server, Client *client,
                               const char *command, char *params);

CommandResult command_nick(Server *server, Client *client, char *params);
CommandResult command_user(Server *server, Client *client, char *params);
CommandResult command_ping(Server *server, Client *client, char *params);
CommandResult command_join(Server *server, Client *client, char *params);
CommandResult command_part(Server *server, Client *client, char *params);
CommandResult command_invite(Server *server, Client *client, char *params);
CommandResult command_kick(Server *server, Client *client, char *params);
CommandResult command_list(Server *server, Client *client, char *params);
CommandResult command_mode(Server *server, Client *client, char *params);
CommandResult command_names(Server *server, Client *client, char *params);
CommandResult command_notice(Server *server, Client *client, char *params);
CommandResult command_privmsg(Server *server, Client *client, char *params);
CommandResult command_topic(Server *server, Client *client, char *params);
CommandResult command_who(Server *server, Client *client, char *params);
CommandResult command_whois(Server *server, Client *client, char *params);
CommandResult command_quit(Server *server, Client *client, char *params);

const char *command_reply_nick(const Client *client);
int command_require_registered(Client *client);
void command_maybe_register(Server *server, Client *client);
void command_send_names(Channel *channel, Client *client);

#endif /* IRCD_COMMANDS_H */
