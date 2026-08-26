/**
 * @file nickserv.c
 * @brief Direct /NICKSERV command alias for the virtual NickServ service.
 */

#include "commands.h"
#include "ircv3.h"
#include "memoserv.h"
#include "message_policy.h"
#include "nickserv.h"
#include "numerics.h"
#include "presence.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

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

static NickServRegistrationThrottle *throttle_slot(NickServRegistrationThrottle *slots,
                                                    const char *ip, time_t now,
                                                    unsigned int window) {
    NickServRegistrationThrottle *free_slot = NULL;
    size_t i;
    if (slots == NULL || ip == NULL || *ip == '\0' || window == 0U) return NULL;
    for (i = 0U; i < IRCD_NICKSERV_REGISTRATION_THROTTLE_SLOTS; ++i) {
        NickServRegistrationThrottle *slot = &slots[i];
        if (slot->ip[0] != '\0' && strcmp(slot->ip, ip) == 0) {
            if (slot->window_start == 0 || now < slot->window_start ||
                now - slot->window_start >= (time_t)window) {
                slot->window_start = now;
                slot->count = 0U;
            }
            return slot;
        }
        if (slot->ip[0] == '\0' || slot->window_start == 0 || now < slot->window_start ||
            now - slot->window_start >= (time_t)window) {
            if (free_slot == NULL) free_slot = slot;
        }
    }
    if (free_slot == NULL) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    (void)snprintf(free_slot->ip, sizeof(free_slot->ip), "%s", ip);
    free_slot->window_start = now;
    return free_slot;
}

int server_nickserv_registration_allowed(Server *server, const char *ip,
                                         time_t now, int consume) {
    NickServRegistrationThrottle *slot;
    unsigned int limit;
    if (server == NULL || ip == NULL || *ip == '\0') return 0;
    limit = server->config.nickserv_registrations_per_ip;
    if (limit == 0U) return 1;
    slot = throttle_slot(server->nickserv_registration_throttles, ip, now,
                         server->config.nickserv_registration_window_seconds);
    if (slot == NULL || slot->count >= limit) return 0;
    if (consume) ++slot->count;
    return 1;
}

static int nickserv_mail_request_allowed(Server *server, const char *ip,
                                         time_t now) {
    NickServRegistrationThrottle *slot;
    unsigned int limit;
    if (server == NULL || ip == NULL || *ip == '\0') return 0;
    limit = server->config.nickserv_mail_requests_per_ip;
    if (limit == 0U) return 1;
    slot = throttle_slot(server->nickserv_mail_throttles, ip, now,
                         server->config.nickserv_mail_window_seconds);
    if (slot == NULL || slot->count >= limit) return 0;
    ++slot->count;
    return 1;
}

static int mail_producing_request(char *copy) {
    char *command;
    char *first;
    char *second;
    if (copy == NULL) return 0;
    command = strtok(copy, " ");
    first = command != NULL ? strtok(NULL, " ") : NULL;
    second = first != NULL ? strtok(NULL, " ") : NULL;
    if (command == NULL) return 0;
    if (strcasecmp(command, "SET") == 0)
        return first != NULL && strcasecmp(first, "EMAIL") == 0;
    if (strcasecmp(command, "RESET") == 0)
        return first != NULL && second == NULL;
    return 0;
}

void command_nickserv_message(Server *server, Client *client, char *text) {
    int was_identified;
    int registering = 0;
    int mail_request = 0;
    time_t now;
    char old_nick[IRC_NICK_MAX + 1U] = "";
    char command_copy[IRCD_MESSAGE_BUFFER_SIZE];
    char mail_copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *service_command;
    Client *recover_target;

    if (server == NULL || client == NULL || text == NULL) return;
    (void)snprintf(command_copy, sizeof(command_copy), "%s", text);
    (void)snprintf(mail_copy, sizeof(mail_copy), "%s", text);
    service_command = strtok(command_copy, " ");
    registering = service_command != NULL && strcasecmp(service_command, "REGISTER") == 0;
    mail_request = mail_producing_request(mail_copy);
    now = time(NULL);
    if (registering && client->account_name[0] == '\0' &&
        !client_mode_has(client->modes, CLIENT_MODE_NETADMIN) &&
        !server_nickserv_registration_allowed(server, client->real_ip, now, 0)) {
        client_sendf(client, ":NickServ!service@%s NOTICE %s :Registration rate limit reached for your IP address; try again later.",
                     server->config.server_name, client->nick);
        snotice_broadcast(server, SNOTICE_REGISTRATIONS | SNOTICE_FLOOD,
                          "NickServ registration throttled: nick=%s real_ip=%s limit=%u/%us",
                          client->nick, client->real_ip,
                          server->config.nickserv_registrations_per_ip,
                          server->config.nickserv_registration_window_seconds);
        return;
    }
    if (mail_request && !client_mode_has(client->modes, CLIENT_MODE_NETADMIN) &&
        !nickserv_mail_request_allowed(server, client->real_ip, now)) {
        client_sendf(client, ":NickServ!service@%s NOTICE %s :Email request rate limit reached for your IP address; try again later.",
                     server->config.server_name, client->nick);
        snotice_broadcast(server, SNOTICE_REGISTRATIONS | SNOTICE_FLOOD,
                          "NickServ email request throttled: nick=%s real_ip=%s limit=%u/%us",
                          client->nick, client->real_ip,
                          server->config.nickserv_mail_requests_per_ip,
                          server->config.nickserv_mail_window_seconds);
        return;
    }
    was_identified = client->account_name[0] != '\0';
    recover_target = recover_target_before(server, text, old_nick, sizeof(old_nick));
    nickserv_handle_message(server, client, text);
    recover_presence_after(server, recover_target, old_nick);
    if (!was_identified && client->account_name[0] != '\0') {
        ircv3_account_notify(client);
        memoserv_notify_unread(server, client);
        if (registering) {
            if (!client_mode_has(client->modes, CLIENT_MODE_NETADMIN))
                (void)server_nickserv_registration_allowed(server, client->real_ip, now, 1);
            snotice_broadcast(server, SNOTICE_REGISTRATIONS,
                              "NickServ registration: account=%s nick=%s real_ip=%s",
                              client->account_name, client->nick, client->real_ip);
        }
    }
}

CommandResult command_nickserv(Server *server, Client *client, char *params) {
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || *params == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "NICKSERV");
        return COMMAND_KEEP_CLIENT;
    }
    command_nickserv_message(server, client, params);
    return COMMAND_KEEP_CLIENT;
}
