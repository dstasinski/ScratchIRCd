/** @file flash.c @brief Server-originated operator announcements via numeric 343. */

#include "commands.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

static int flash_text_fits(const Server *server, const char *text) {
    char maximum_nick[IRC_NICK_MAX + 1U];
    int written;

    if (server == NULL || text == NULL) return 0;
    memset(maximum_nick, 'n', IRC_NICK_MAX);
    maximum_nick[IRC_NICK_MAX] = '\0';
    written = snprintf(NULL, 0, RPL_FLASH, server->config.server_name,
                       maximum_nick, text);
    return written >= 0 && (size_t)written <= IRC_LINE_CONTENT_MAX;
}

static void send_flash(Server *server, Client *target, const char *text) {
    if (target == NULL || !target->registered) return;
    client_sendf(target, RPL_FLASH, server->config.server_name,
                 target->nick, text);
}

CommandResult command_flash(Server *server, Client *client, char *params) {
    char *targets;
    char *text;
    char *target_name;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || (targets = strtok(params, " ")) == NULL ||
        (text = strtok(NULL, "")) == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "FLASH");
        return COMMAND_KEEP_CLIENT;
    }
    if (*text == ':') ++text;
    if (*targets == '\0' || *text == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "FLASH");
        return COMMAND_KEEP_CLIENT;
    }
    if (!flash_text_fits(server, text)) {
        client_sendf(client,
                     ":%s 417 %s FLASH :Message would exceed the IRC line limit",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    if (strcmp(targets, "*") == 0) {
        size_t index;
        for (index = 0U; index < server->client_count; ++index)
            send_flash(server, server->clients[index], text);
        return COMMAND_KEEP_CLIENT;
    }

    for (target_name = strtok(targets, ","); target_name != NULL;
         target_name = strtok(NULL, ",")) {
        if (strchr(IRC_CHANNEL_PREFIXES, target_name[0]) != NULL) {
            Channel *channel = hash_get(&server->channels_by_name, target_name);
            ChannelMember *member;
            if (channel == NULL) {
                client_sendf(client, ERR_NOSUCHCHANNEL,
                             server->config.server_name, client->nick, target_name);
                continue;
            }
            for (member = channel->members; member != NULL; member = member->next)
                send_flash(server, member->client, text);
        } else {
            Client *target = hash_get(&server->clients_by_nick, target_name);
            if (target == NULL || !target->registered) {
                client_sendf(client, ERR_NOSUCHNICK,
                             server->config.server_name, client->nick, target_name);
                continue;
            }
            send_flash(server, target, text);
        }
    }
    return COMMAND_KEEP_CLIENT;
}

/*
 * Lightweight, non-cryptographic game rolls.  This process-local PRNG is
 * intentionally inexpensive; it is not suitable for security decisions.
 */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

static unsigned int game_roll(unsigned int maximum) {
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL) ^ (unsigned int)clock());
        seeded = 1;
    }
    return 1U + (unsigned int)((double)rand() / ((double)RAND_MAX + 1.0) * maximum);
}

static int game_number(const char *text, unsigned int maximum,
                       unsigned int *value) {
    unsigned long parsed;
    char *end;
    if (text == NULL || *text == '\0' || *text == '-' || *text == '+') return 0;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed < 1UL || parsed > maximum) return 0;
    *value = (unsigned int)parsed;
    return 1;
}

static void game_send(Server *server, Client *recipient, const char *target,
                      const char *key, const char *result) {
    if (recipient == NULL || !recipient->registered) return;
    client_sendf(recipient, RPL_KEYVALUE, server->config.server_name,
                 recipient->nick, target, key, "*", result);
}

static CommandResult game_command(Server *server, Client *client, char *params,
                                  const char *key) {
    char *first = NULL, *second = NULL, *extra = NULL;
    const char *target_name = NULL;
    unsigned int count = 0U;
    char result[384];
    size_t used = 0U;
    unsigned int i;
    Client *target_client = NULL;
    Channel *target_channel = NULL;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params != NULL) {
        first = strtok(params, " ");
        second = strtok(NULL, " ");
        extra = strtok(NULL, " ");
    }
    if (extra != NULL) goto usage;
    if (strcmp(key, "COIN") == 0) {
        if (second != NULL) goto usage;
        target_name = first;
    } else {
        if (second != NULL) {
            target_name = first;
            if (!game_number(second, strcmp(key, "DICE") == 0 ? 100U : INT_MAX, &count))
                goto usage;
        } else if (!game_number(first, strcmp(key, "DICE") == 0 ? 100U : INT_MAX, &count)) {
            goto usage;
        }
    }
    if (target_name != NULL) {
        if (!client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) {
            client_sendf(client, ERR_NOPRIVILEGES,
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (strchr(IRC_CHANNEL_PREFIXES, target_name[0]) != NULL) {
            target_channel = hash_get(&server->channels_by_name, target_name);
            if (target_channel == NULL) {
                client_sendf(client, ERR_NOSUCHCHANNEL,
                             server->config.server_name, client->nick, target_name);
                return COMMAND_KEEP_CLIENT;
            }
        } else {
            target_client = hash_get(&server->clients_by_nick, target_name);
            if (target_client == NULL || !target_client->registered) {
                client_sendf(client, ERR_NOSUCHNICK,
                             server->config.server_name, client->nick, target_name);
                return COMMAND_KEEP_CLIENT;
            }
        }
    }
    if (strcmp(key, "COIN") == 0) {
        (void)snprintf(result, sizeof(result), "%s",
                       game_roll(2U) == 1U ? "heads" : "tails");
    } else if (strcmp(key, "RANDOM") == 0) {
        (void)snprintf(result, sizeof(result), "%u", game_roll(count));
    } else {
        result[0] = '\0';
        for (i = 0U; i < count; ++i) {
            int written = snprintf(result + used, sizeof(result) - used,
                                   "%s%u", i == 0U ? "" : " ", game_roll(6U));
            if (written < 0 || (size_t)written >= sizeof(result) - used) return COMMAND_KEEP_CLIENT;
            used += (size_t)written;
        }
    }
    if (target_channel != NULL) {
        ChannelMember *member;
        for (member = target_channel->members; member != NULL; member = member->next)
            game_send(server, member->client, target_channel->name, key, result);
    } else if (target_client != NULL) {
        game_send(server, target_client, target_client->nick, key, result);
    } else {
        game_send(server, client, client->nick, key, result);
    }
    return COMMAND_KEEP_CLIENT;

usage:
    client_sendf(client, ":%s 461 %s %s :Usage: %s",
                 server->config.server_name, client->nick, key,
                 strcmp(key, "COIN") == 0 ? "COIN [target]" :
                 strcmp(key, "DICE") == 0 ? "DICE [target] <count:1-100>" :
                                             "RANDOM [target] <max:1-2147483647>");
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_coin(Server *server, Client *client, char *params) {
    return game_command(server, client, params, "COIN");
}
CommandResult command_dice(Server *server, Client *client, char *params) {
    return game_command(server, client, params, "DICE");
}
CommandResult command_random(Server *server, Client *client, char *params) {
    return game_command(server, client, params, "RANDOM");
}
