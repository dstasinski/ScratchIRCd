/** @file globops.c @brief Operator messaging and selective server notices. */
#include "commands.h"
#include "message_policy.h"
#include "modes.h"
#include "numerics.h"
#include "oper.h"

#include <string.h>

static CommandResult send_oper_message(Server *server, Client *client,
                                       char *params, const char *command) {
    char *text = params;
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN) ||
        !client_mode_has(client->modes, CLIENT_MODE_GLOBALS)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (text == NULL || *text == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name, client->nick, command);
        return COMMAND_KEEP_CLIENT;
    }
    if (*text == ':') ++text;
    oper_message_broadcast(server, client, command, text);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_globops(Server *server, Client *client, char *params) { return send_oper_message(server,client,params,"GLOBOPS"); }
CommandResult command_locops(Server *server, Client *client, char *params) { return send_oper_message(server,client,params,"LOCOPS"); }

CommandResult command_snotice(Server *server, Client *client, char *params) {
    char masks[32];
    char sign = '+';
    const char *p;
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || *params == '\0') {
        (void)snotice_mask_format(client->snotice_mask,masks,sizeof(masks));
        client_sendf(client,":%s NOTICE %s :SNOTICE mask: +%s",server->config.server_name,client->nick,masks);
        return COMMAND_KEEP_CLIENT;
    }
    for (p=params;*p!='\0'&&*p!=' ';++p) {
        SnoticeMask bit;
        if (*p=='+'||*p=='-') { sign=*p; continue; }
        if (*p=='*') bit=SNOTICE_ALL;
        else bit=snotice_mask_for_letter(*p);
        if (bit==0U) {
            client_sendf(client,":%s NOTICE %s :Unknown SNOTICE mask letter: %c",server->config.server_name,client->nick,*p);
            continue;
        }
        if (sign=='+') client->snotice_mask|=bit; else client->snotice_mask&=~bit;
    }
    if (client->snotice_mask!=0U) client->modes=client_mode_add(client->modes,CLIENT_MODE_SERVER_NOTICES);
    else client->modes=client_mode_remove(client->modes,CLIENT_MODE_SERVER_NOTICES);
    (void)snotice_mask_format(client->snotice_mask,masks,sizeof(masks));
    client_sendf(client,":%s NOTICE %s :SNOTICE mask now: +%s",server->config.server_name,client->nick,masks);
    return COMMAND_KEEP_CLIENT;
}
