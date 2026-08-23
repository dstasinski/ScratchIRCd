/** @file nospoof.c @brief Connection PING-cookie and CTCP metadata probes. */
#include "nospoof.h"
#include "commands.h"
#include "config.h"
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static void hex_cookie(char *out,size_t out_size){unsigned char bytes[IRCD_NOSPOOF_COOKIE_BYTES];size_t i;if(out==NULL||out_size<IRCD_NOSPOOF_COOKIE_HEX_LEN+1U)return;if(RAND_bytes(bytes,(int)sizeof(bytes))!=1){out[0]='\0';return;}for(i=0U;i<sizeof(bytes);++i)(void)snprintf(out+i*2U,out_size-i*2U,"%02x",bytes[i]);}
static void request_version(Server *server,Client *client){if(server==NULL||client==NULL||!server->config.nospoof_enabled||!client->nospoof_verified||client->version_requested||client->nick[0]=='\0')return;client_sendf(client,":%s PRIVMSG %s :\001VERSION\001",server->config.server_name,client->nick);client->version_requested=1;}
void nospoof_start(Server *server,Client *client){if(server==NULL||client==NULL||!server->config.nospoof_enabled||client->nospoof_started||client->nick[0]=='\0')return;hex_cookie(client->nospoof_cookie,sizeof(client->nospoof_cookie));if(client->nospoof_cookie[0]=='\0')return;client->nospoof_started=1;client->nospoof_deadline=time(NULL)+(time_t)server->config.nospoof_timeout_seconds;client_sendf(client,"PING :%s",client->nospoof_cookie);}
void nospoof_request_website(Server *server,Client *client){if(server==NULL||client==NULL||!server->config.nospoof_enabled||!client->nospoof_verified||!client->webirc.active||client->website_requested||client->nick[0]=='\0')return;client_sendf(client,":%s PRIVMSG %s :\001WEBSITE\001",server->config.server_name,client->nick);client->website_requested=1;}
static const char *clean_param(const char *params){if(params==NULL)return NULL;while(*params==' ')++params;if(*params==':')++params;return params;}
int nospoof_handle_pong(Server *server,Client *client,const char *params){const char *token=clean_param(params);if(client==NULL||!client->nospoof_started||client->nospoof_verified||token==NULL)return 0;{const char *last=strrchr(token,' ');if(last!=NULL)token=clean_param(last+1);}if(strcmp(token,client->nospoof_cookie)!=0)return 0;client->nospoof_verified=1;client->nospoof_cookie[0]='\0';request_version(server,client);nospoof_request_website(server,client);command_maybe_register(server,client);return 1;}
static int capture_ctcp(char *dest,size_t dest_size,const char *text,const char *name){size_t name_len=strlen(name);const char *value;size_t length;if(text==NULL||text[0]!='\001'||strncasecmp(text+1,name,name_len)!=0)return 0;value=text+1+name_len;if(*value!=' '&&*value!='\001')return 0;if(*value==' ')++value;length=strlen(value);if(length!=0U&&value[length-1U]=='\001')--length;if(length>=dest_size)length=dest_size-1U;memcpy(dest,value,length);dest[length]='\0';return 1;}
int nospoof_handle_notice(Server *server,Client *client,const char *params){char copy[IRCD_MESSAGE_BUFFER_SIZE];char *target,*text;if(server==NULL||client==NULL||params==NULL||!server->config.nospoof_enabled)return 0;(void)snprintf(copy,sizeof(copy),"%s",params);target=strtok(copy," ");text=strtok(NULL,"");if(target==NULL||text==NULL||strcasecmp(target,server->config.server_name)!=0)return 0;if(*text==':')++text;if(client->version_requested&&capture_ctcp(client->client_version,sizeof(client->client_version),text,"VERSION")){client->version_received=1;return 1;}if(client->webirc.active&&client->website_requested&&capture_ctcp(client->client_website,sizeof(client->client_website),text,"WEBSITE")){client->website_received=1;return 1;}return 0;}
int nospoof_version_restricted(const Server *server,const Client *client){return server!=NULL&&client!=NULL&&server->config.nospoof_enabled&&client->version_requested&&!client->version_received;}
