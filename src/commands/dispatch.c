/** @file dispatch.c @brief Maps IRC command names to command handlers. */
#include "commands.h"
#include "message_policy.h"
#include "numerics.h"
#include "nospoof.h"
#include "oper.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define COMMAND_BUDGET_BURST 20U
#define COMMAND_BUDGET_REFILL_PER_SECOND 4U
#define COMMAND_GLOBAL_BUDGET_BURST 200U
#define COMMAND_GLOBAL_BUDGET_REFILL_PER_SECOND 40U
#define COMMAND_THROTTLE_SNOTICE_SECONDS 5

/* General event-loop flood budget. This is intentionally much more generous
 * than the expensive-command bucket: normal IRC bursts should pass, while a
 * sustained cheap-command/message flood is eventually disconnected. */
#define FLOOD_BUDGET_BURST 80U
#define FLOOD_BUDGET_REFILL_PER_SECOND 20U
#define FLOOD_VIOLATION_WINDOW_SECONDS 5
#define FLOOD_VIOLATIONS_BEFORE_DISCONNECT 3U

typedef struct CommandEntry {
    const char *name;
    CommandHandler handler;
    unsigned int cost;
} CommandEntry;

/*
 * Cost 0 means ordinary traffic/control and is never throttled here.
 * The weighted bucket is reserved for commands that enumerate server state,
 * hit persistent databases, or perform expensive password verification.
 */
static const CommandEntry command_table[] = {
{"ADMIN",command_admin,1},{"AUTHENTICATE",command_authenticate,5},{"AWAY",command_away,0},{"CAP",command_cap,0},{"CHANSERV",command_chanserv,2},{"CHATHISTORY",command_chathistory,5},{"CSDROP",command_csdrop,3},{"CSINFO",command_csinfo,3},{"CSSET",command_csset,3},{"DEAF",command_deaf,0},{"DIE",command_die,0},{"GEOBAN",command_geoban,0},{"GLOBOPS",command_globops,0},{"IDENTIFY",command_identify,5},{"INFO",command_info,2},{"INVITE",command_invite,0},{"ISON",command_ison,1},{"JOIN",command_join,0},{"KICK",command_kick,0},{"KILL",command_kill,0},{"KLINE",command_kline,0},{"KNOCK",command_knock,0},{"LINKS",command_links,2},{"LIST",command_list,5},{"LOCOPS",command_locops,0},{"LUSERS",command_lusers,1},{"MEMOSERV",command_memoserv,2},{"MODE",command_mode,0},{"MOTD",command_motd,1},{"MSINFO",command_msinfo,3},{"MSPURGE",command_mspurge,3},{"MUTE",command_mute,0},{"NAMES",command_names,3},{"NICK",command_nick,0},{"NICKSERV",command_nickserv,2},{"NOTICE",command_notice,0},{"NSDROP",command_nsdrop,3},{"NSINFO",command_nsinfo,3},{"NSSET",command_nsset,3},{"OPER",command_oper,5},{"OPERADD",command_operadd,0},{"OPERDEL",command_operdel,0},{"OPERLIST",command_operlist,2},{"OPERSET",command_operset,0},{"PART",command_part,0},{"PASS",command_pass,0},{"PING",command_ping,0},{"PONG",command_pong,0},{"PRIVMSG",command_privmsg,0},{"QUIT",command_quit,0},{"REHASH",command_rehash,0},{"RESTART",command_restart,0},{"RULES",command_rules,1},{"SAJOIN",command_sajoin,0},{"SAMODE",command_samode,0},{"SAPART",command_sapart,0},{"SETHOST",command_sethost,0},{"SETIDENT",command_setident,0},{"SETNAME",command_setname,0},{"SILENCE",command_silence,1},{"SNOTICE",command_snotice,0},{"STATS",command_stats,1},{"TAGMSG",command_tagmsg,0},{"TIME",command_time,1},{"TOPIC",command_topic,0},{"UNGEOBAN",command_ungeoban,0},{"USER",command_user,0},{"USERHOST",command_userhost,1},{"USERIP",command_userip,1},{"VERSION",command_version,1},{"WALLOPS",command_wallops,0},{"WATCH",command_watch,1},{"WEBIRC",command_webirc,0},{"WHO",command_who,4},{"WHOIS",command_whois,2},{"WHOWAS",command_whowas,2},{"ZLINE",command_zline,0}};

static unsigned int general_flood_cost(const char *command) {
    if (command == NULL) return 1U;
    /* QUIT must always be accepted, and PONG must never be delayed because it
     * is the client's response to server liveness checks. */
    if (strcasecmp(command, "QUIT") == 0 || strcasecmp(command, "PONG") == 0)
        return 0U;

    /* These commands can fan out to users/channels or cause substantial
     * membership churn, so charge two ordinary tokens. */
    if (strcasecmp(command, "PRIVMSG") == 0 || strcasecmp(command, "NOTICE") == 0 ||
        strcasecmp(command, "JOIN") == 0 || strcasecmp(command, "PART") == 0 ||
        strcasecmp(command, "NICK") == 0 || strcasecmp(command, "MODE") == 0 ||
        strcasecmp(command, "TOPIC") == 0 || strcasecmp(command, "KICK") == 0 ||
        strcasecmp(command, "INVITE") == 0 || strcasecmp(command, "KNOCK") == 0)
        return 2U;

    /* Unknown commands also reach this function and therefore cost one token. */
    return 1U;
}

static void send_bounded_command_numeric(Server *server, Client *client,
                                         int numeric, const char *command,
                                         const char *text) {
    const char *reply_nick;
    int base_length;
    size_t length;
    if (server == NULL || client == NULL || text == NULL) return;
    if (command == NULL || *command == '\0') command = "*";
    reply_nick = command_reply_nick(client);
    base_length = snprintf(NULL, 0, ":%s %03d %s  :%s",
                           server->config.server_name, numeric,
                           reply_nick, text);
    if (base_length < 0 || (size_t)base_length > IRC_LINE_CONTENT_MAX) return;
    length = strlen(command);
    if (length > IRC_LINE_CONTENT_MAX - (size_t)base_length)
        length = IRC_LINE_CONTENT_MAX - (size_t)base_length;
    client_sendf(client, ":%s %03d %s %.*s :%s",
                 server->config.server_name, numeric, reply_nick,
                 (int)length, command, text);
}

static int general_flood_allow(Server *server, Client *client,
                               const char *command, unsigned int cost) {
    time_t now;
    time_t elapsed;
    unsigned long refill;

    if (cost == 0U || client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN))
        return 1;

    now = time(NULL);
    if (client->flood_budget_updated == 0) {
        client->flood_budget_updated = now;
        client->flood_budget_tokens = FLOOD_BUDGET_BURST;
    } else if (now > client->flood_budget_updated) {
        elapsed = now - client->flood_budget_updated;
        refill = (unsigned long)elapsed * FLOOD_BUDGET_REFILL_PER_SECOND;
        if (refill >= FLOOD_BUDGET_BURST - client->flood_budget_tokens)
            client->flood_budget_tokens = FLOOD_BUDGET_BURST;
        else
            client->flood_budget_tokens += (unsigned int)refill;
        client->flood_budget_updated = now;
    }

    if (client->flood_budget_tokens >= cost) {
        client->flood_budget_tokens -= cost;
        return 1;
    }

    if (client->flood_violation_window == 0 ||
        now - client->flood_violation_window >= FLOOD_VIOLATION_WINDOW_SECONDS) {
        client->flood_violation_window = now;
        client->flood_violation_count = 1U;
    } else {
        ++client->flood_violation_count;
    }

    if (client->flood_violation_count >= FLOOD_VIOLATIONS_BEFORE_DISCONNECT) {
        snotice_broadcast(server, SNOTICE_FLOOD,
                          "Client flood disconnected: nick=%s real_ip=%s command=%s",
                          command_reply_nick(client), client->real_ip,
                          command != NULL ? command : "-");
        (void)snprintf(client->quit_reason, sizeof(client->quit_reason),
                       "Excess flood");
        client_sendf(client, ":%s ERROR :Excess flood",
                     server->config.server_name);
        return 0;
    }

    send_bounded_command_numeric(server, client, 263, command,
                                 "Flood protection - please slow down");
    return -1;
}

static void refill_weighted_budget(time_t now, time_t *updated,
                                   unsigned int *tokens, unsigned int burst,
                                   unsigned int refill_per_second) {
    time_t elapsed;
    unsigned long refill;
    if (*updated == 0) {
        *updated = now;
        *tokens = burst;
        return;
    }
    if (now <= *updated) return;
    elapsed = now - *updated;
    refill = (unsigned long)elapsed * refill_per_second;
    if (*tokens >= burst || refill >= (unsigned long)(burst - *tokens))
        *tokens = burst;
    else
        *tokens += (unsigned int)refill;
    *updated = now;
}

int command_expensive_allow(Server *server, Client *client,
                            const char *command, unsigned int cost) {
    time_t now;

    if (cost == 0U || client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN))
        return 1;

    now = time(NULL);
    refill_weighted_budget(now, &client->command_budget_updated,
                           &client->command_budget_tokens,
                           COMMAND_BUDGET_BURST,
                           COMMAND_BUDGET_REFILL_PER_SECOND);
    refill_weighted_budget(now, &server->command_global_budget_updated,
                           &server->command_global_budget_tokens,
                           COMMAND_GLOBAL_BUDGET_BURST,
                           COMMAND_GLOBAL_BUDGET_REFILL_PER_SECOND);

    if (client->command_budget_tokens < cost) {
        client_sendf(client, ":%s 263 %s %s :Please wait before repeating this command",
                     server->config.server_name, command_reply_nick(client), command);
        if (client->command_throttle_notice_time == 0 ||
            now - client->command_throttle_notice_time >= COMMAND_THROTTLE_SNOTICE_SECONDS) {
            snotice_broadcast(server, SNOTICE_FLOOD,
                              "Expensive command throttled: nick=%s real_ip=%s command=%s",
                              command_reply_nick(client), client->real_ip, command);
            client->command_throttle_notice_time = now;
        }
        return 0;
    }

    if (server->command_global_budget_tokens < cost) {
        client_sendf(client, ":%s 263 %s %s :Server busy - please retry this expensive command shortly",
                     server->config.server_name, command_reply_nick(client), command);
        if (server->command_global_throttle_notice_time == 0 ||
            now - server->command_global_throttle_notice_time >= COMMAND_THROTTLE_SNOTICE_SECONDS) {
            snotice_broadcast(server, SNOTICE_FLOOD,
                              "Global expensive-command budget exhausted: nick=%s real_ip=%s command=%s",
                              command_reply_nick(client), client->real_ip, command);
            server->command_global_throttle_notice_time = now;
        }
        return 0;
    }

    client->command_budget_tokens -= cost;
    server->command_global_budget_tokens -= cost;
    return 1;
}

/* A client can submit a syntactically legal <=510-byte MODE line whose
 * source-prefixed server rebroadcast would exceed the IRC 510-byte content
 * limit. Reject such channel MODE changes before command_mode() can mutate
 * channel state; clients can split an oversized batch into smaller commands. */
static int channel_mode_wire_fits(const Client *client, const char *params) {
    const char *target;
    size_t wire_len;

    if (client == NULL || params == NULL) return 1;
    target = params;
    while (*target == ' ') ++target;
    if (*target != '#' && *target != '&') return 1;

    /* :nick!user@host MODE <params> -- CRLF is not part of the 510-byte
     * content allowance. All identity fields are fixed-size Client members,
     * so these additions cannot overflow size_t in practice. */
    wire_len = 1U + strlen(client->nick) + 1U + strlen(client->user) + 1U +
               strlen(client->display_host) + 6U + strlen(params);
    return wire_len <= 510U;
}

CommandResult command_dispatch(Server *server,Client *client,const char *command,char *params){
    size_t index;
    int flood_result;
    if(server==NULL||client==NULL||command==NULL)return COMMAND_KEEP_CLIENT;
    /* Any complete client command is activity before a liveness challenge.
     * Once PING is outstanding, only its matching PONG may clear the deadline. */
    if(!client->ping_pending)client->last_activity=time(NULL);
    if(server->config.nospoof_enabled&&client->nospoof_started&&!client->nospoof_verified&&time(NULL)>=client->nospoof_deadline){
        snotice_broadcast(server,SNOTICE_SECURITY,"No-spoof timeout: %s [real_ip=%s]",command_reply_nick(client),client->real_ip);
        client_sendf(client,":%s ERROR :No-spoof PING timeout",server->config.server_name);
        return COMMAND_DISCONNECT_CLIENT;
    }

    flood_result = general_flood_allow(server, client, command,
                                       general_flood_cost(command));
    if (flood_result == 0) return COMMAND_DISCONNECT_CLIENT;
    if (flood_result < 0) return COMMAND_KEEP_CLIENT;

    if (strcasecmp(command, "MODE") == 0 && !channel_mode_wire_fits(client, params)) {
        client_sendf(client,
                     ":%s 417 %s MODE :MODE change would exceed the IRC line limit; split the change",
                     server->config.server_name, command_reply_nick(client));
        return COMMAND_KEEP_CLIENT;
    }

    for(index=0U;index<sizeof(command_table)/sizeof(command_table[0]);++index){
        if(strcasecmp(command,command_table[index].name)==0){
            if(!command_expensive_allow(server,client,command,command_table[index].cost))
                return COMMAND_KEEP_CLIENT;
            return command_table[index].handler(server,client,params);
        }
    }
    send_bounded_command_numeric(server, client, 421, command, "Unknown command");
    return COMMAND_KEEP_CLIENT;
}
