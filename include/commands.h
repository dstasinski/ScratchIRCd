#ifndef IRCD_COMMANDS_H
#define IRCD_COMMANDS_H
#include "server.h"
typedef enum CommandResult{COMMAND_KEEP_CLIENT=0,COMMAND_DISCONNECT_CLIENT=1}CommandResult;typedef CommandResult(*CommandHandler)(Server*,Client*,char*);
CommandResult command_dispatch(Server*,Client*,const char*,char*);
#define DECL(name) CommandResult command_##name(Server*,Client*,char*)
DECL(admin);DECL(authenticate);DECL(away);DECL(cap);DECL(chathistory);DECL(chanserv);DECL(csinfo);DECL(csset);DECL(csdrop);DECL(identify);DECL(invite);DECL(ison);DECL(join);DECL(kick);DECL(kill);DECL(kline);DECL(list);DECL(lusers);DECL(mode);DECL(motd);DECL(names);DECL(nick);DECL(nickserv);DECL(notice);DECL(nsdrop);DECL(nsinfo);DECL(nsset);DECL(oper);DECL(operadd);DECL(operdel);DECL(operlist);DECL(operset);DECL(part);DECL(pass);DECL(ping);DECL(privmsg);DECL(quit);DECL(rehash);DECL(restart);DECL(rules);DECL(sajoin);DECL(samode);DECL(sapart);DECL(sethost);DECL(setident);DECL(setname);DECL(topic);DECL(user);DECL(userhost);DECL(userip);DECL(wallops);DECL(webirc);DECL(who);DECL(whois);DECL(zline);
#undef DECL
const char *command_reply_nick(const Client*);int command_require_registered(Client*);void command_maybe_register(Server*,Client*);void command_send_names(Channel*,Client*);
#endif
