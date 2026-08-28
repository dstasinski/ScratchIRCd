#ifndef IRCD_COMMANDS_H
#define IRCD_COMMANDS_H
#include "server.h"
typedef enum CommandResult { COMMAND_KEEP_CLIENT = 0, COMMAND_DISCONNECT_CLIENT = 1 } CommandResult;
typedef CommandResult (*CommandHandler)(Server *, Client *, char *);
CommandResult command_dispatch(Server *, Client *, const char *, char *);
#define CMD(name) CommandResult command_##name(Server *, Client *, char *)
CMD(admin); CMD(authenticate); CMD(away); CMD(cap); CMD(chanserv); CMD(chathistory); CMD(csdrop); CMD(csinfo); CMD(csset); CMD(deaf); CMD(die); CMD(geoban); CMD(globops); CMD(identify); CMD(info); CMD(invite); CMD(ison); CMD(join); CMD(kick); CMD(kill); CMD(kline); CMD(knock); CMD(links); CMD(list); CMD(locops); CMD(lusers); CMD(memoserv); CMD(msinfo); CMD(mspurge); CMD(mode); CMD(motd); CMD(mute); CMD(names); CMD(nick); CMD(nickserv); CMD(notice); CMD(nsdrop); CMD(nsinfo); CMD(nsset); CMD(oper); CMD(operadd); CMD(operdel); CMD(operlist); CMD(operset); CMD(part); CMD(pass); CMD(ping); CMD(pong); CMD(privmsg); CMD(quit); CMD(rehash); CMD(restart); CMD(rules); CMD(sajoin); CMD(samode); CMD(sapart); CMD(sethost); CMD(setident); CMD(setname); CMD(silence); CMD(snotice); CMD(stats); CMD(time); CMD(topic); CMD(ungeoban); CMD(user); CMD(userhost); CMD(userip); CMD(version); CMD(wallops); CMD(watch); CMD(webirc); CMD(who); CMD(whois); CMD(whowas); CMD(zline);
#undef CMD
const char *command_reply_nick(const Client *client);
int command_require_registered(Client *client);
void command_maybe_register(Server *server, Client *client);
void command_send_names(Server *server, Channel *channel, Client *client);
void command_nickserv_message(Server *server, Client *client, char *text);
void command_memoserv_message(Server *server, Client *client, char *text);
/** Largest topic guaranteed to fit a 332 reply for any legal nick/channel. */
size_t command_topic_limit(const Server *server);
/** Set the runtime server name used by pre-registration numerics. */
void command_common_set_server_name(const char *server_name);
/** Reset process-local command caches/handles for shutdown or in-process RESTART. */
void command_common_reset_state(void);
void command_motd_reset_cache(void);
void command_rules_reset_cache(void);
/** Charge the shared weighted budget used for database/enumeration work. */
int command_expensive_allow(Server *server, Client *client,
                            const char *command, unsigned int cost);
#endif