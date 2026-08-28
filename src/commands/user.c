/**
 * @file user.c
 * @brief Implementation of the IRC USER command.
 *
 * USER supplies the ident and real-name fields. Registration is attempted
 * after parsing, but common.c waits for NICK and asynchronous DNS completion.
 */

#include "commands.h"
#include "numerics.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int valid_user_char(unsigned char ch) {
    return isalnum(ch) || ch == '-' || ch == '_' || ch == '.';
}

static int valid_username(const char *user) {
    size_t index;
    size_t length;
    if (user == NULL) return 0;
    length = strlen(user);
    if (length == 0U || length > IRC_USER_MAX) return 0;
    for (index = 0U; index < length; ++index)
        if (!valid_user_char((unsigned char)user[index])) return 0;
    return 1;
}

static int valid_realname(const char *realname) {
    size_t index;
    size_t length;
    if (realname == NULL) return 0;
    length = strlen(realname);
    if (length == 0U || length > IRC_REALNAME_MAX) return 0;
    for (index = 0U; index < length; ++index) {
        unsigned char ch = (unsigned char)realname[index];
        if (ch < 0x20U || ch == 0x7fU) return 0;
    }
    return 1;
}

CommandResult command_user(Server *server, Client *client, char *params) {
    char *user;
    char *mode;
    char *unused;
    char *realname;

    if (client->registered || client->user[0] != '\0') {
        client_sendf(client, ERR_ALREADYREGISTRED,
                     server->config.server_name, command_reply_nick(client));
        return COMMAND_KEEP_CLIENT;
    }

    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, command_reply_nick(client), "USER");
        return COMMAND_KEEP_CLIENT;
    }

    user = strtok(params, " ");
    mode = strtok(NULL, " ");
    unused = strtok(NULL, " ");
    realname = strtok(NULL, "");

    if (user == NULL || mode == NULL || unused == NULL || realname == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, command_reply_nick(client), "USER");
        return COMMAND_KEEP_CLIENT;
    }

    if (*realname == ':') ++realname;

    if (!valid_username(user) || !valid_realname(realname)) {
        client_sendf(client, ":%s 468 %s :Invalid USER identity",
                     server->config.server_name, command_reply_nick(client));
        return COMMAND_KEEP_CLIENT;
    }

    (void)snprintf(client->user, sizeof(client->user), "%s", user);
    (void)snprintf(client->realname, sizeof(client->realname), "%s", realname);

    command_maybe_register(server, client);
    return COMMAND_KEEP_CLIENT;
}
