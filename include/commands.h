#ifndef IRCD_COMMANDS_H
#define IRCD_COMMANDS_H

#include "server.h"

typedef enum CommandResult {
    COMMAND_KEEP_CLIENT = 0,
    COMMAND_DISCONNECT_CLIENT = 1
} CommandResult;

typedef CommandResult (*CommandHandler)(Server *server, Client *client, char *params);

CommandResult command_dispatch(Server *server, Client *client,
                               const char *command, char *params);
CommandResult command_admin(Server *, Client *, char *);
CommandResult command_authenticate(Server *, Client *, char *);
CommandResult command_away(Server *, Client *, char *);
CommandResult command_cap(Server *, Client *, char *);
CommandResult command_chanserv(Server *, Client *, char *);
CommandResult command_chathistory(Server *, Client *, char *);
CommandResult command_csdrop(Server *, Client *, char *);
CommandResult command_csinfo(Server *, Client *, char *);
CommandResult command_csset(Server *, Client *, char *);
CommandResult command_deaf(Server *, Client *, char *);
CommandResult command_geoban(Server *, Client *, char *);
CommandResult command_globops(Server *, Client *, char *);
CommandResult command_identify(Server *, Client *, char *);
CommandResult command_invite(Server *, Client *, char *);
CommandResult command_ison(Server *, Client *, char *);
CommandResult command_join(Server *, Client *, char *);
CommandResult command_kick(Server *, Client *, char *);
CommandResult command_kill(Server *, Client *, char *);
CommandResult command_kline(Server *, Client *, char *);
CommandResult command_knock(Server *, Client *, char *);
CommandResult command_list(Server *, Client *, char *);
CommandResult command_locops(Server *, Client *, char *);
CommandResult command_lusers(Server *, Client *, char *);
CommandResult command_memoserv(Server *, Client *, char *);
CommandResult command_msinfo(Server *, Client *, char *);
CommandResult command_mspurge(Server *, Client *, char *);
CommandResult command_mode(Server *, Client *, char *);
CommandResult command_motd(Server *, Client *, char *);
CommandResult command_mute(Server *, Client *, char *);
CommandResult command_names(Server *, Client *, char *);
CommandResult command_nick(Server *, Client *, char *);
CommandResult command_nickserv(Server *, Client *, char *);
CommandResult command_notice(Server *, Client *, char *);
CommandResult command_nsdrop(Server *, Client *, char *);
CommandResult command_nsinfo(Server *, Client *, char *);
CommandResult command_nsset(Server *, Client *, char *);
CommandResult command_oper(Server *, Client *, char *);
CommandResult command_operadd(Server *, Client *, char *);
CommandResult command_operdel(Server *, Client *, char *);
CommandResult command_operlist(Server *, Client *, char *);
CommandResult command_operset(Server *, Client *, char *);
CommandResult command_part(Server *, Client *, char *);
CommandResult command_pass(Server *, Client *, char *);
CommandResult command_ping(Server *, Client *, char *);
CommandResult command_privmsg(Server *, Client *, char *);
CommandResult command_quit(Server *, Client *, char *);
CommandResult command_rehash(Server *, Client *, char *);
CommandResult command_restart(Server *, Client *, char *);
CommandResult command_rules(Server *, Client *, char *);
CommandResult command_sajoin(Server *, Client *, char *);
CommandResult command_samode(Server *, Client *, char *);
CommandResult command_sapart(Server *, Client *, char *);
CommandResult command_sethost(Server *, Client *, char *);
CommandResult command_setident(Server *, Client *, char *);
CommandResult command_setname(Server *, Client *, char *);
CommandResult command_silence(Server *, Client *, char *);
CommandResult command_topic(Server *, Client *, char *);
CommandResult command_ungeoban(Server *, Client *, char *);
CommandResult command_user(Server *, Client *, char *);
CommandResult command_userhost(Server *, Client *, char *);
CommandResult command_userip(Server *, Client *, char *);
CommandResult command_wallops(Server *, Client *, char *);
CommandResult command_watch(Server *, Client *, char *);
CommandResult command_webirc(Server *, Client *, char *);
CommandResult command_who(Server *, Client *, char *);
CommandResult command_whois(Server *, Client *, char *);
CommandResult command_whowas(Server *, Client *, char *);
CommandResult command_zline(Server *, Client *, char *);

const char *command_reply_nick(const Client *client);
int command_require_registered(Client *client);
void command_maybe_register(Server *server, Client *client);
void command_send_names(Channel *channel, Client *client);

#endif
