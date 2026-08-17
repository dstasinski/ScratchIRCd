#ifndef IRCD_COMMANDS_H
#define IRCD_COMMANDS_H

/*
 * commands.h
 *
 * Public interface for IRC command dispatch and command implementations.
 *
 * The network layer parses complete IRC lines and passes the command token and
 * parameter text to command_dispatch().  The dispatcher performs only command
 * selection; protocol behavior lives in one of the source files under
 * src/commands/.  This keeps command policy separate from sockets, clients,
 * channels, and hash-table management.
 */

#include "server.h"

/**
 * Result returned by an IRC command handler.
 *
 * Most commands leave the connection active.  Commands such as QUIT request
 * that the server remove the connection after the current command returns.
 */
typedef enum CommandResult {
    COMMAND_KEEP_CLIENT = 0,       /**< Continue servicing the client. */
    COMMAND_DISCONNECT_CLIENT = 1  /**< Disconnect after handler returns. */
} CommandResult;

/** Common function signature implemented by every IRC command handler. */
typedef CommandResult (*CommandHandler)(Server *server, Client *client,
                                        char *params);

/** Dispatch one already-parsed command token to its implementation. */
CommandResult command_dispatch(Server *server, Client *client,
                               const char *command, char *params);

/* Individual command entry points. */
CommandResult command_nick(Server *server, Client *client, char *params);
CommandResult command_user(Server *server, Client *client, char *params);
CommandResult command_ping(Server *server, Client *client, char *params);
CommandResult command_join(Server *server, Client *client, char *params);
CommandResult command_part(Server *server, Client *client, char *params);
CommandResult command_privmsg(Server *server, Client *client, char *params);
CommandResult command_quit(Server *server, Client *client, char *params);

/* Shared command helpers. */
const char *command_reply_nick(const Client *client);
int command_require_registered(Client *client);
void command_maybe_register(Client *client);
void command_send_names(Channel *channel, Client *client);

#endif /* IRCD_COMMANDS_H */
