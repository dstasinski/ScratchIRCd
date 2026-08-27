/** @file webirc.c @brief Authenticate trusted WebIRC gateways and establish end-user identity. */
#include "commands.h"
#include "message_policy.h"
#include "modes.h"
#include "nospoof.h"
#include "oper.h"
#include <arpa/inet.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int numeric_ip_equal(const char *left,const char *right){struct in_addr l4,r4;struct in6_addr l6,r6;if(inet_pton(AF_INET,left,&l4)==1&&inet_pton(AF_INET,right,&r4)==1)return memcmp(&l4,&r4,sizeof(l4))==0;if(inet_pton(AF_INET6,left,&l6)==1&&inet_pton(AF_INET6,right,&r6)==1)return memcmp(&l6,&r6,sizeof(l6))==0;return 0;}
static int password_equal(const char *left,const char *right){size_t a=strlen(left),b=strlen(right);return a==b&&CRYPTO_memcmp(left,right,a)==0;}
static const WebIrcGatewayConfig *authorized_gateway(const ServerConfig *config,const char *peer_ip,const char *password){size_t i;for(i=0U;i<config->webirc_gateway_count;++i){const WebIrcGatewayConfig *gateway=&config->webirc_gateways[i];if(numeric_ip_equal(gateway->ip,peer_ip)&&password_equal(gateway->password,password))return gateway;}return NULL;}

int server_reassign_client_id(Server *server,Client *client){
    char old_key[32],new_key[32];
    uint64_t new_id;
    if(server==NULL||client==NULL)return -1;
    new_id=server->next_client_id+1U;
    (void)snprintf(old_key,sizeof(old_key),"%llu",(unsigned long long)client->id);
    (void)snprintf(new_key,sizeof(new_key),"%llu",(unsigned long long)new_id);
    if(hash_set(&server->clients_by_id,new_key,client)!=0)return -1;
    (void)hash_remove(&server->clients_by_id,old_key);
    server->next_client_id=new_id;
    client->id=new_id;
    return 0;
}

CommandResult command_webirc(Server *server,Client *client,char *params){char *password,*gateway_name,*supplied_host,*supplied_ip;char peer_ip[IRC_IP_MAX+1U];int family;struct in_addr v4;struct in6_addr v6;if(client->registered||client->webirc.active||params==NULL){snotice_broadcast(server,SNOTICE_WEBIRC,"Rejected WEBIRC use from %s: command must be used once before registration",client->real_ip);client_sendf(client,":%s ERROR :WEBIRC must be used once before registration",server->config.server_name);return COMMAND_DISCONNECT_CLIENT;}password=strtok(params," ");gateway_name=strtok(NULL," ");supplied_host=strtok(NULL," ");supplied_ip=strtok(NULL," ");if(password==NULL||gateway_name==NULL||supplied_host==NULL||supplied_ip==NULL||strtok(NULL," ")!=NULL){snotice_broadcast(server,SNOTICE_WEBIRC,"Rejected WEBIRC from %s: invalid parameters",client->real_ip);client_sendf(client,":%s ERROR :Invalid WEBIRC parameters",server->config.server_name);return COMMAND_DISCONNECT_CLIENT;}(void)snprintf(peer_ip,sizeof(peer_ip),"%s",client->real_ip);if(authorized_gateway(&server->config,peer_ip,password)==NULL){snotice_broadcast(server,SNOTICE_WEBIRC,"Rejected WEBIRC authentication from gateway %s",peer_ip);client_sendf(client,":%s ERROR :Unauthorized WEBIRC gateway",server->config.server_name);return COMMAND_DISCONNECT_CLIENT;}if(inet_pton(AF_INET,supplied_ip,&v4)==1)family=AF_INET;else if(inet_pton(AF_INET6,supplied_ip,&v6)==1)family=AF_INET6;else{snotice_broadcast(server,SNOTICE_WEBIRC,"Rejected WEBIRC from gateway %s: invalid end-user IP %s",peer_ip,supplied_ip);client_sendf(client,":%s ERROR :WEBIRC supplied invalid client IP",server->config.server_name);return COMMAND_DISCONNECT_CLIENT;}if(server_connection_limit_reached(server,supplied_ip,client)){snotice_broadcast(server,SNOTICE_SECURITY|SNOTICE_WEBIRC,"WEBIRC end-user connection limit exceeded: end-user=%s gateway=%s",supplied_ip,peer_ip);client_sendf(client,":%s ERROR :Too many concurrent connections from your IP address",server->config.server_name);return COMMAND_DISCONNECT_CLIENT;}if(server_reassign_client_id(server,client)!=0){snotice_broadcast(server,SNOTICE_SECURITY|SNOTICE_WEBIRC,"Rejected WEBIRC from gateway %s: unable to reindex end-user connection",peer_ip);client_sendf(client,":%s ERROR :WEBIRC connection setup failed",server->config.server_name);return COMMAND_DISCONNECT_CLIENT;}(void)snprintf(client->webirc.gateway_ip,sizeof(client->webirc.gateway_ip),"%s",peer_ip);(void)snprintf(client->webirc.gateway_name,sizeof(client->webirc.gateway_name),"%s",gateway_name);(void)snprintf(client->webirc.supplied_host,sizeof(client->webirc.supplied_host),"%s",supplied_host);client->webirc.active=1;client->address_family=family;(void)snprintf(client->real_ip,sizeof(client->real_ip),"%s",supplied_ip);client->real_host[0]='\0';(void)snprintf(client->display_host,sizeof(client->display_host),"%s",supplied_ip);client->modes=client_mode_add(client->modes,CLIENT_MODE_WEBIRC);memset(&client->geoip,0,sizeof(client->geoip));client->geoip_complete=0;client->dnsbl_state=CLIENT_DNSBL_NONE;client->dnsbl_deadline=0;client->dns_state=CLIENT_DNS_PENDING;client->dns_deadline=time(NULL)+(time_t)server->config.dns_timeout_seconds;if(dns_resolver_submit(&server->dns,client->id,family,client->real_ip)!=0)client->dns_state=CLIENT_DNS_FAILED;snotice_broadcast(server,SNOTICE_WEBIRC,"WEBIRC authenticated: end-user=%s gateway=%s gateway-name=%s",client->real_ip,peer_ip,gateway_name);nospoof_request_website(server,client);return COMMAND_KEEP_CLIENT;}
