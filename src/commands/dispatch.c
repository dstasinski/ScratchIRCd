/** @file dispatch.c @brief Maps IRC command names to command handlers. */
#include "commands.h"
#include "message_policy.h"
#include "numerics.h"
#include "nospoof.h"
#include "oper.h"
#include <stddef.h>
#include <strings.h>
#include <time.h>

#define COMMAND_BUDGET_BURST 20U
#define COMMAND_BUDGET_REFILL_PER_SECOND 4U
#define COMMAND_THROTTLE_SNOTICE_SECONDS 5

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
{"ADMIN",command_admin,1},{"AUTHENTICATE",command_authenticate,5},{"AWAY",command_away,0},{"CAP",command_cap,0},{"CHANSERV",command_chanserv,2},{"CHATHISTORY",command_chathistory,5},{"CSDROP",command_csdrop,3},{"CSINFO",command_csinfo,3},{"CSSET",command_csset,3},{"DEAF",command_deaf,0},{"DIE",command_die,0},{"GEOBAN",command_geoban,0},{"GLOBOPS",command_globops,0},{"IDENTIFY",command_identify,5},{"INFO",command_info,2},{"INVITE",command_invite,0},{"ISON",command_ison,1},{"JOIN",command_join,0},{"KICK",command_kick,0},{"KILL",command_kill,0},{"KLINE",command_kline,0},{"KNOCK",command_knock,0},{"LINKS",command_links,2},{"LIST",command_list,5},{"LOCOPS",command_locops,0},{"LUSERS",command_lusers,1},{"MEMOSERV",command_memoserv,2},{"MODE",command_mode,0},{"MOTD",command_motd,1},{"MSINFO",command_msinfo,3},{"MSPURGE",command_mspurge,3},{"MUTE",command_mute,0},{"NAMES",command_names,3},{"NICK",command_nick,0},{"NICKSERV",command_nickserv,2},{"NOTICE",command_notice,0},{"NSDROP",command_nsdrop,3},{"NSINFO",command_nsinfo,3},{"NSSET",command_nsset,3},{"OPER",command_oper,5},{"OPERADD",command_operadd,0},{"OPERDEL",command_operdel,0},{"OPERLIST",command_operlist,2},{"OPERSET",command_operset,0},{"PART",command_part,0},{"PASS",command_pass,0},{"PING",command_ping,0},{"PONG",command_pong,0},{"PRIVMSG",command_privmsg,0},{"QUIT",command_quit,0},{"REHASH",command_rehash,0},{"RESTART",command_restart,0},{"RULES",command_rules,1},{"SAJOIN",command_sajoin,0},{"SAMODE",command_samode,0},{"SAPART",command_sapart,0},{"SETHOST",command_sethost,0},{"SETIDENT",command_setident,0},{"SETNAME",command_setname,0},{"SILENCE",command_silence,1},{"SNOTICE",command_snotice,0},{"STATS",command_stats,5},{"TIME",command_time,1},{"TOPIC",command_topic,0},{"UNGEOBAN",command_ungeoban,0},{"USER",command_user,0},{"USERHOST",command_userhost,1},{"USERIP",command_userip,1},{"VERSION",command_version,1},{"WALLOPS",command_wallops,0},{"WATCH",command_watch,1},{"WEBIRC",command_webirc,0},{"WHO",command_who,4},{"WHOIS",command_whois,2},{"WHOWAS",command_whowas,2},{"ZLINE",command_zline,0}};

static int command_budget_allow(Server *server, Client *client,
                                const char *command, unsigned int cost) {
    time_t now;
    time_t elapsed;
    unsigned long refill;

    if (cost == 0U || client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN))
        return 1;

    now = time(NULL);
    if (client->command_budget_updated == 0) {
        client->command_budget_updated = now;
        client->command_budget_tokens = COMMAND_BUDGET_BURST;
    } else if (now > client->command_budget_updated) {
        elapsed = now - client->command_budget_updated;
        refill = (unsigned long)elapsed * COMMAND_BUDGET_REFILL_PER_SECOND;
        if (refill >= COMMAND_BUDGET_BURST - client->command_budget_tokens)
            client->command_budget_tokens = COMMAND_BUDGET_BURST;
        else
            client->command_budget_tokens += (unsigned int)refill;
        client->command_budget_updated = now;
    }

    if (client->command_budget_tokens >= cost) {
        client->command_budget_tokens -= cost;
        return 1;
    }

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

CommandResult command_dispatch(Server *server,Client *client,const char *command,char *params){
    size_t index;
    if(server==NULL||client==NULL||command==NULL)return COMMAND_KEEP_CLIENT;
    if(server->config.nospoof_enabled&&client->nospoof_started&&!client->nospoof_verified&&time(NULL)>=client->nospoof_deadline){
        snotice_broadcast(server,SNOTICE_SECURITY,"No-spoof timeout: %s [real_ip=%s]",command_reply_nick(client),client->real_ip);
        client_sendf(client,":%s ERROR :No-spoof PING timeout",server->config.server_name);
        return COMMAND_DISCONNECT_CLIENT;
    }
    for(index=0U;index<sizeof(command_table)/sizeof(command_table[0]);++index){
        if(strcasecmp(command,command_table[index].name)==0){
            if(!command_budget_allow(server,client,command,command_table[index].cost))
                return COMMAND_KEEP_CLIENT;
            return command_table[index].handler(server,client,params);
        }
    }
    client_sendf(client,ERR_UNKNOWNCOMMAND,server->config.server_name,command_reply_nick(client),command);
    return COMMAND_KEEP_CLIENT;
}
