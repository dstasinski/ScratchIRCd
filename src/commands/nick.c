/** @file nick.c @brief IRC NICK command. */
#include "commands.h"
#include "config.h"
#include "nickserv.h"
#include "nospoof.h"
#include "numerics.h"
#include "presence.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
static int valid_nick_char(unsigned char ch){return isalnum(ch)||ch=='-'||ch=='_'||ch=='['||ch==']'||ch=='\\'||ch=='`'||ch=='^'||ch=='{'||ch=='}'||ch=='|';}
static int valid_nickname(const char *nick){size_t index,length;if(nick==NULL)return 0;length=strlen(nick);if(length==0U||length>IRC_NICK_MAX)return 0;if(!(isalpha((unsigned char)nick[0])||strchr("[]\\`_^{|}",nick[0])!=NULL))return 0;for(index=1U;index<length;++index)if(!valid_nick_char((unsigned char)nick[index]))return 0;return 1;}
static void broadcast_nick_change(Client *client,const char *old_nick){char message[IRCD_MESSAGE_BUFFER_SIZE];ClientChannelLink *link;(void)snprintf(message,sizeof(message),":%s!%s@%s NICK :%s\r\n",old_nick,client->user,client->display_host,client->nick);for(link=client->channels;link!=NULL;link=link->next)channel_broadcast(link->channel,NULL,message);}
CommandResult command_nick(Server *server,Client *client,char *params){Client *existing;char old_nick[IRC_NICK_MAX+1U];int was_registered;if(params==NULL||*params=='\0'){client_sendf(client,ERR_NONICKNAMEGIVEN,server->config.server_name,command_reply_nick(client));return COMMAND_KEEP_CLIENT;}params[strcspn(params," ")]='\0';if(!valid_nickname(params)){client_sendf(client,ERR_ERRONEUSNICKNAME,server->config.server_name,command_reply_nick(client),params);return COMMAND_KEEP_CLIENT;}if(service_nickname_reserved(params)){client_sendf(client,ERR_RESERVEDNICK,server->config.server_name,command_reply_nick(client),params);return COMMAND_KEEP_CLIENT;}existing=hash_get(&server->clients_by_nick,params);if(existing!=NULL&&existing!=client){client_sendf(client,ERR_NICKNAMEINUSE,server->config.server_name,command_reply_nick(client),params);return COMMAND_KEEP_CLIENT;}was_registered=client->registered;(void)snprintf(old_nick,sizeof(old_nick),"%s",client->nick);if(was_registered&&old_nick[0]!='\0'){presence_whowas_record(server,client,old_nick);presence_watch_offline(server,client,old_nick);}if(client->nick[0]!='\0')(void)hash_remove(&server->clients_by_nick,client->nick);(void)snprintf(client->nick,sizeof(client->nick),"%s",params);if(hash_set(&server->clients_by_nick,client->nick,client)!=0){client->nick[0]='\0';return COMMAND_KEEP_CLIENT;}if(was_registered&&old_nick[0]!='\0'){broadcast_nick_change(client,old_nick);presence_watch_online(server,client);}if(!was_registered){nospoof_start(server,client);nospoof_request_website(server,client);}command_maybe_register(server,client);return COMMAND_KEEP_CLIENT;}
