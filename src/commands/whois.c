/** @file whois.c @brief IRC WHOIS with operator-only audit metadata. */
#include "commands.h"
#include "modes.h"
#include "numerics.h"
#include "visibility.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static void send_whois_user(Server *server,Client *client,const Client *target){
    char realname[IRC_REALNAME_MAX+1U];
    int overhead;
    size_t limit;
    size_t length;
    if(server==NULL||client==NULL||target==NULL)return;
    overhead=snprintf(NULL,0,RPL_WHOISUSER,server->config.server_name,client->nick,
                      target->nick,target->user,target->display_host,"");
    if(overhead<0||(size_t)overhead>IRC_LINE_CONTENT_MAX)return;
    limit=IRC_LINE_CONTENT_MAX-(size_t)overhead;
    if(limit>IRC_REALNAME_MAX)limit=IRC_REALNAME_MAX;
    length=strlen(target->realname);
    if(length>limit)length=limit;
    memcpy(realname,target->realname,length);
    realname[length]='\0';
    client_sendf(client,RPL_WHOISUSER,server->config.server_name,client->nick,
                 target->nick,target->user,target->display_host,realname);
}

static int whois_channels_payload_fits(const Server *server,const Client *client,
                                       const Client *target,const char *channels){
    int written;
    if(server==NULL||client==NULL||target==NULL||channels==NULL)return 0;
    written=snprintf(NULL,0,RPL_WHOISCHANNELS,server->config.server_name,
                     client->nick,target->nick,channels);
    return written>=0&&(size_t)written<=IRC_LINE_CONTENT_MAX;
}

static void send_whois_channels(Server *server,Client *client,Client *target){
    char channels[IRC_LINE_CONTENT_MAX+1U]="";
    char candidate[IRC_LINE_CONTENT_MAX+1U];
    ClientChannelLink *link;
    size_t used=0U;

    if(server==NULL||client==NULL||target==NULL)return;
    for(link=target->channels;link!=NULL;link=link->next){
        Channel *channel=link->channel;
        ChannelMember *membership;
        char token[IRC_CHANNEL_NAME_MAX+2U];
        char prefix;
        int token_written;
        int candidate_written;
        if(channel==NULL||!visibility_whois_channel(client,target,channel))continue;
        membership=channel_find_member(channel,target);
        prefix=membership!=NULL?channel_privilege_prefix(membership->privileges):'\0';
        token_written=prefix!='\0'
            ?snprintf(token,sizeof(token),"%c%s",prefix,channel->name)
            :snprintf(token,sizeof(token),"%s",channel->name);
        if(token_written<0||(size_t)token_written>=sizeof(token))continue;
        candidate_written=snprintf(candidate,sizeof(candidate),"%s%s%s",
                                   channels,used?" ":"",token);
        if(candidate_written>=0&&(size_t)candidate_written<sizeof(candidate)&&
           whois_channels_payload_fits(server,client,target,candidate)){
            memcpy(channels,candidate,(size_t)candidate_written+1U);
            used=(size_t)candidate_written;
            continue;
        }
        if(used!=0U)
            client_sendf(client,RPL_WHOISCHANNELS,server->config.server_name,
                         client->nick,target->nick,channels);
        if(!whois_channels_payload_fits(server,client,target,token)){
            channels[0]='\0';
            used=0U;
            continue;
        }
        (void)snprintf(channels,sizeof(channels),"%s",token);
        used=strlen(channels);
    }
    if(used!=0U)
        client_sendf(client,RPL_WHOISCHANNELS,server->config.server_name,
                     client->nick,target->nick,channels);
}

CommandResult command_whois(Server *server,Client *client,char *params){
    char *target_name;
    Client *target;
    if(command_require_registered(client))return COMMAND_KEEP_CLIENT;
    if(params==NULL||(target_name=strtok(params," ,"))==NULL){
        client_sendf(client,ERR_NONICKNAMEGIVEN,server->config.server_name,client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    target=hash_get(&server->clients_by_nick,target_name);
    if(target==NULL){
        client_sendf(client,ERR_NOSUCHNICK,server->config.server_name,client->nick,target_name);
        client_sendf(client,RPL_ENDOFWHOIS,server->config.server_name,client->nick,target_name);
        return COMMAND_KEEP_CLIENT;
    }
    if(target!=client&&client_mode_has(target->modes,CLIENT_MODE_WHOIS_NOTICE)&&
       client_mode_has(target->modes,CLIENT_MODE_OPER|CLIENT_MODE_NETADMIN))
        client_sendf(target,":%s NOTICE %s :*** %s!%s@%s did a /WHOIS on you",
                     server->config.server_name,target->nick,client->nick,
                     client->user,client->display_host);
    send_whois_user(server,client,target);
    client_sendf(client,RPL_WHOISSERVER,server->config.server_name,client->nick,
                 target->nick,server->config.server_name,server->config.network_name);
    if(target->away[0]!='\0')
        client_sendf(client,RPL_AWAY,server->config.server_name,client->nick,
                     target->nick,target->away);
    if(client_mode_has(target->modes,CLIENT_MODE_REGISTERED)){
        if(target->account_name[0]!='\0'&&strcasecmp(target->nick,target->account_name)!=0){
            char account_text[IRC_NICK_MAX+32U];
            (void)snprintf(account_text,sizeof(account_text),"is logged in as %s",target->account_name);
            client_sendf(client,RPL_WHOISSPECIAL,server->config.server_name,
                         client->nick,target->nick,account_text);
        }else
            client_sendf(client,RPL_WHOISREGNICK,server->config.server_name,
                         client->nick,target->nick);
    }
    if(client_mode_has(target->modes,CLIENT_MODE_BOT))
        client_sendf(client,RPL_WHOISBOT,server->config.server_name,client->nick,target->nick);
    if(client_mode_has(target->modes,CLIENT_MODE_HELPOP))
        client_sendf(client,RPL_WHOISHELPOP,server->config.server_name,client->nick,target->nick);
    if(client_mode_has(target->modes,CLIENT_MODE_NETADMIN)&&
       (!client_mode_has(target->modes,CLIENT_MODE_HIDE_OPER)||visibility_is_oper(client)))
        client_sendf(client,RPL_WHOISADMIN,server->config.server_name,client->nick,target->nick);
    else if(client_mode_has(target->modes,CLIENT_MODE_OPER)&&
            (!client_mode_has(target->modes,CLIENT_MODE_HIDE_OPER)||visibility_is_oper(client)))
        client_sendf(client,RPL_WHOISOPERATOR,server->config.server_name,client->nick,target->nick);
    if(client_mode_has(target->modes,CLIENT_MODE_SECURE))
        client_sendf(client,RPL_WHOISSECURE,server->config.server_name,client->nick,
                     target->nick,"is using a secure connection");
    send_whois_channels(server,client,target);
    if(!client_mode_has(target->modes,CLIENT_MODE_HIDE_IDLE)||client==target||visibility_is_oper(client)){
        time_t now=time(NULL);
        long idle=now>=target->last_activity?(long)(now-target->last_activity):0L;
        client_sendf(client,RPL_WHOISIDLE,server->config.server_name,client->nick,
                     target->nick,idle,(long)target->signon_time);
    }
    if(visibility_is_oper(client)){
        const char *real_host=target->real_host[0]!='\0'?target->real_host:target->real_ip;
        client_sendf(client,RPL_WHOISHOST,server->config.server_name,client->nick,
                     target->nick,real_host,target->real_ip);
        if(target->version_received)
            client_sendf(client,RPL_WHOISVERSION,server->config.server_name,
                         client->nick,target->nick,target->client_version);
        if(target->webirc.active&&target->website_received)
            client_sendf(client,RPL_WHOISWEBSITE,server->config.server_name,
                         client->nick,target->nick,target->client_website);
    }
    client_sendf(client,RPL_ENDOFWHOIS,server->config.server_name,client->nick,target->nick);
    return COMMAND_KEEP_CLIENT;
}
