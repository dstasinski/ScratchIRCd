/**
 * @file authenticate.c
 * @brief IRCv3 SASL AUTHENTICATE command using the NickServ account database.
 *
 * This first SASL implementation supports PLAIN. Credentials are decoded from
 * one base64 payload and authenticated through nickserv_identify(), ensuring
 * SASL and IDENTIFY produce identical account, +r and vhost state.
 */

#include "commands.h"
#include "nickserv.h"
#include "numerics.h"

#include <openssl/evp.h>
#include <string.h>
#include <strings.h>

static void sasl_fail(Server *server, Client *client) {
    client->sasl_state = CLIENT_SASL_FAILED;
    client_sendf(client, ERR_SASLFAIL, server->config.server_name,
                 command_reply_nick(client));
}

CommandResult command_authenticate(Server *server, Client *client, char *params) {
    unsigned char decoded[512];
    int decoded_len;
    char *authzid;
    char *authcid;
    char *password;
    size_t payload_len;

    if (server == NULL || client == NULL || params == NULL || *params == '\0')
        return COMMAND_KEEP_CLIENT;
    params[strcspn(params, " ")] = '\0';

    if ((client->capabilities & CLIENT_CAP_SASL) == 0U) {
        sasl_fail(server, client);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcmp(params, "*") == 0) {
        client->sasl_state = CLIENT_SASL_FAILED;
        client_sendf(client, ERR_SASLABORTED, server->config.server_name,
                     command_reply_nick(client));
        return COMMAND_KEEP_CLIENT;
    }

    if (client->sasl_state == CLIENT_SASL_NONE ||
        client->sasl_state == CLIENT_SASL_FAILED) {
        if (strcasecmp(params, "PLAIN") != 0) {
            sasl_fail(server, client);
            return COMMAND_KEEP_CLIENT;
        }
        client->sasl_state = CLIENT_SASL_PLAIN_WAIT_DATA;
        client_sendf(client, "AUTHENTICATE +");
        return COMMAND_KEEP_CLIENT;
    }

    if (client->sasl_state != CLIENT_SASL_PLAIN_WAIT_DATA) {
        sasl_fail(server, client);
        return COMMAND_KEEP_CLIENT;
    }

    payload_len = strlen(params);
    if (payload_len == 0U || payload_len > 400U || (payload_len % 4U) != 0U) {
        client_sendf(client, ERR_SASLTOOLONG, server->config.server_name,
                     command_reply_nick(client));
        client->sasl_state = CLIENT_SASL_FAILED;
        return COMMAND_KEEP_CLIENT;
    }
    decoded_len = EVP_DecodeBlock(decoded, (const unsigned char *)params, (int)payload_len);
    if (decoded_len <= 0 || (size_t)decoded_len >= sizeof(decoded)) {
        sasl_fail(server, client);
        return COMMAND_KEEP_CLIENT;
    }
    while (payload_len > 0U && params[payload_len - 1U] == '=') {
        --decoded_len;
        --payload_len;
    }
    decoded[decoded_len] = '\0';

    authzid = (char *)decoded;
    authcid = authzid + strlen(authzid) + 1U;
    if (authcid >= (char *)decoded + decoded_len) {
        sasl_fail(server, client);
        return COMMAND_KEEP_CLIENT;
    }
    password = authcid + strlen(authcid) + 1U;
    if (password > (char *)decoded + decoded_len || *authcid == '\0' || *password == '\0') {
        sasl_fail(server, client);
        return COMMAND_KEEP_CLIENT;
    }
    if (*authzid != '\0' && strcasecmp(authzid, authcid) != 0) {
        sasl_fail(server, client);
        return COMMAND_KEEP_CLIENT;
    }

    if (!nickserv_identify(server, client, authcid, password)) {
        sasl_fail(server, client);
        return COMMAND_KEEP_CLIENT;
    }

    client->sasl_state = CLIENT_SASL_COMPLETE;
    client_sendf(client, RPL_LOGGEDIN, server->config.server_name,
                 command_reply_nick(client),
                 client->nick[0] != '\0' ? client->nick : "*",
                 client->user[0] != '\0' ? client->user : "*",
                 client->display_host, client->account_name, client->account_name);
    client_sendf(client, RPL_SASLSUCCESS, server->config.server_name,
                 command_reply_nick(client));
    return COMMAND_KEEP_CLIENT;
}
