/**
 * @file nickserv.c
 * @brief Direct /NICKSERV command alias for the virtual NickServ service.
 *
 * IRC clients commonly translate /NICKSERV locally into PRIVMSG NickServ.
 * ScratchIRCd also accepts NICKSERV directly so RECOVER/GHOST/RESET and the
 * rest of the service command set work consistently across clients.
 */

#include "commands.h"
#include "ircv3.h"
#include "memoserv.h"
#include "nickserv.h"
#include "numerics.h"
#include "presence.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static Client *recover_target_before(Server *server, const char *params,
                                     char *old_nick, size_t old_nick_size) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *command;
    char *nick;
    char *action;
    Client *target;

    if (server == NULL || params == NULL || old_nick == NULL || old_nick_size == 0U)
        return NULL;
    (void)snprintf(copy, sizeof(copy), "%s", params);
    command = strtok(copy, " ");
    nick = command != NULL ? strtok(NULL, " ") : NULL;
    action = nick != NULL ? strtok(NULL, " ") : NULL;
    if (command == NULL || nick == NULL || strcasecmp(command, "RECOVER") != 0 ||
        action != NULL) return NULL;
    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) return NULL;
    (void)snprintf(old_nick, old_nick_size, "%s", target->nick);
    return target;
}

static void recover_presence_after(Server *server, Client *target,
                                   const char *old_nick) {
    if (server == NULL || target == NULL || old_nick == NULL || *old_nick == '\0' ||
        server_find_client_by_id(server, target->id) != target ||
        strcmp(target->nick, old_nick) == 0) return;
    presence_whowas_record(server, target, old_nick);
    presence_watch_offline(server, target, old_nick);
    presence_watch_online(server, target);
}

CommandResult command_nickserv(Server *server, Client *client, char *params) {
    int was_identified;
    char old_nick[IRC_NICK_MAX + 1U] = "";
    Client *recover_target;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || *params == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "NICKSERV");
        return COMMAND_KEEP_CLIENT;
    }
    was_identified = client->account_name[0] != '\0';
    recover_target = recover_target_before(server, params, old_nick, sizeof(old_nick));
    nickserv_handle_message(server, client, params);
    recover_presence_after(server, recover_target, old_nick);
    if (!was_identified && client->account_name[0] != '\0') {
        ircv3_account_notify(client);
        memoserv_notify_unread(server, client);
    }
    return COMMAND_KEEP_CLIENT;
}
