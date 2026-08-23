/**
 * @file admin.c
 * @brief Implementation of IRC server-information commands.
 */

#include "commands.h"
#include "config.h"
#include "numerics.h"

#include <stdio.h>
#include <time.h>

CommandResult command_admin(Server *server, Client *client, char *params) {
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    if (server->config.admin_location1[0] == '\0' &&
        server->config.admin_location2[0] == '\0' &&
        server->config.admin_email[0] == '\0') {
        client_sendf(client, ERR_NOADMININFO,
                     server->config.server_name, client->nick,
                     server->config.server_name);
        return COMMAND_KEEP_CLIENT;
    }

    client_sendf(client, RPL_ADMINME,
                 server->config.server_name, client->nick,
                 server->config.server_name);
    client_sendf(client, RPL_ADMINLOC1,
                 server->config.server_name, client->nick,
                 server->config.admin_location1);
    client_sendf(client, RPL_ADMINLOC2,
                 server->config.server_name, client->nick,
                 server->config.admin_location2);
    client_sendf(client, RPL_ADMINEMAIL,
                 server->config.server_name, client->nick,
                 server->config.admin_email);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_info(Server *server, Client *client, char *params) {
    char version_line[128];
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    client_sendf(client, RPL_INFOSTART,
                 server->config.server_name, client->nick);
    (void)snprintf(version_line, sizeof(version_line),
                   "ScratchIRCd %s on %s", IRCD_VERSION,
                   server->config.server_name);
    client_sendf(client, RPL_INFO,
                 server->config.server_name, client->nick, version_line);
    client_sendf(client, RPL_INFO,
                 server->config.server_name, client->nick,
                 "Single-server IRC daemon written in C for Linux");
    client_sendf(client, RPL_INFO,
                 server->config.server_name, client->nick,
                 "NickServ, ChanServ, and MemoServ are virtual services");
    client_sendf(client, RPL_ENDOFINFO,
                 server->config.server_name, client->nick);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_links(Server *server, Client *client, char *params) {
    const char *mask = params != NULL && params[0] != '\0' ? params : "*";

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    client_sendf(client, RPL_LINKS,
                 server->config.server_name, client->nick,
                 server->config.server_name, server->config.server_name,
                 0, "ScratchIRCd single-server daemon");
    client_sendf(client, RPL_ENDOFLINKS,
                 server->config.server_name, client->nick, mask);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_time(Server *server, Client *client, char *params) {
    time_t now;
    struct tm local;
    char text[128];
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    now = time(NULL);
    if (localtime_r(&now, &local) == NULL ||
        strftime(text, sizeof(text), "%a %b %d %Y -- %H:%M:%S %Z", &local) == 0U) {
        text[0] = '\0';
    }
    client_sendf(client, RPL_TIME,
                 server->config.server_name, client->nick,
                 server->config.server_name,
                 text[0] != '\0' ? text : "Unknown server time");
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_version(Server *server, Client *client, char *params) {
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    client_sendf(client, ":%s 351 %s %s %s :single-server C11 Linux",
                 server->config.server_name, client->nick,
                 IRCD_VERSION, server->config.server_name);
    return COMMAND_KEEP_CLIENT;
}
