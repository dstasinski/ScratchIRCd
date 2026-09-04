/**
 * @file authenticate.c
 * @brief IRCv3 SASL AUTHENTICATE command using the NickServ account database.
 *
 * This SASL implementation supports PLAIN. Credentials are accumulated from
 * standard 400-byte AUTHENTICATE frames and authenticated through
 * nickserv_identify(), ensuring SASL and IDENTIFY produce identical account,
 * +r and vhost state.
 */

#include "commands.h"
#include "memoserv.h"
#include "nickserv.h"
#include "numerics.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void sasl_reset_payload(Client *client) {
    if (client == NULL) return;
    if (client->sasl_buffer != NULL)
        OPENSSL_cleanse(client->sasl_buffer, client->sasl_buffer_capacity);
    free(client->sasl_buffer);
    client->sasl_buffer = NULL;
    client->sasl_buffer_len = 0U;
    client->sasl_buffer_capacity = 0U;
}

static void sasl_fail(Server *server, Client *client) {
    sasl_reset_payload(client);
    client->sasl_state = CLIENT_SASL_FAILED;
    client_sendf(client, ERR_SASLFAIL, server->config.server_name,
                 command_reply_nick(client));
}

static void sasl_too_long(Server *server, Client *client) {
    sasl_reset_payload(client);
    client->sasl_state = CLIENT_SASL_FAILED;
    client_sendf(client, ERR_SASLTOOLONG, server->config.server_name,
                 command_reply_nick(client));
}

/* Return 1 after appending, 0 for the configured aggregate limit, and -1
 * when storage cannot be allocated. */
static int sasl_append_frame(Client *client, const char *frame,
                             size_t frame_len) {
    size_t required;
    char *resized;

    if (client == NULL || frame == NULL ||
        client->sasl_buffer_len > IRCD_SASL_ENCODED_MAX ||
        frame_len > IRCD_SASL_ENCODED_MAX - client->sasl_buffer_len)
        return 0;

    required = client->sasl_buffer_len + frame_len + 1U;
    if (required > client->sasl_buffer_capacity) {
        resized = realloc(client->sasl_buffer, required);
        if (resized == NULL) return -1;
        client->sasl_buffer = resized;
        client->sasl_buffer_capacity = required;
    }
    if (frame_len != 0U)
        memcpy(client->sasl_buffer + client->sasl_buffer_len,
               frame, frame_len);
    client->sasl_buffer_len += frame_len;
    client->sasl_buffer[client->sasl_buffer_len] = '\0';
    return 1;
}

static int plain_fields(unsigned char *decoded, size_t decoded_len,
                        char **authzid, char **authcid, char **password) {
    unsigned char *end;
    unsigned char *first_separator;
    unsigned char *second_separator;

    if (decoded == NULL || decoded_len == 0U || authzid == NULL ||
        authcid == NULL || password == NULL) return 0;

    end = decoded + decoded_len;
    first_separator = memchr(decoded, '\0', decoded_len);
    if (first_separator == NULL) return 0;

    second_separator = memchr(first_separator + 1, '\0',
                              (size_t)(end - (first_separator + 1)));
    if (second_separator == NULL || second_separator == first_separator + 1 ||
        second_separator + 1 == end) return 0;

    /* PLAIN has exactly three fields. An embedded NUL in the password would
     * otherwise let C-string verification silently ignore trailing bytes. */
    if (memchr(second_separator + 1, '\0',
               (size_t)(end - (second_separator + 1))) != NULL) return 0;

    decoded[decoded_len] = '\0';
    *authzid = (char *)decoded;
    *authcid = (char *)(first_separator + 1);
    *password = (char *)(second_separator + 1);
    return 1;
}

static void sasl_finish_plain(Server *server, Client *client) {
    unsigned char decoded[IRCD_SASL_DECODED_MAX + 1U];
    int decoded_len;
    size_t decoded_size;
    size_t padding = 0U;
    char *authzid;
    char *authcid;
    char *password;
    const char *payload;
    size_t payload_len = client->sasl_buffer_len;

    payload = client->sasl_buffer != NULL ? client->sasl_buffer : "";
    if (payload_len == 0U || (payload_len % 4U) != 0U) {
        goto rejected;
    }

    while (padding < payload_len && payload[payload_len - padding - 1U] == '=')
        ++padding;
    if (padding > 2U ||
        memchr(payload, '=', payload_len - padding) != NULL) {
        goto rejected;
    }

    decoded_len = EVP_DecodeBlock(decoded, (const unsigned char *)payload,
                                  (int)payload_len);
    if (decoded_len <= 0 || (size_t)decoded_len < padding) {
        goto rejected;
    }
    decoded_size = (size_t)decoded_len - padding;
    if (decoded_size >= sizeof(decoded) ||
        !plain_fields(decoded, decoded_size, &authzid, &authcid, &password)) {
        goto rejected;
    }
    if (*authzid != '\0' && strcasecmp(authzid, authcid) != 0) {
        goto rejected;
    }

    if (!nickserv_identify(server, client, authcid, password)) {
        goto rejected;
    }

    OPENSSL_cleanse(decoded, sizeof(decoded));
    sasl_reset_payload(client);
    memoserv_notify_unread(server, client);
    client->sasl_state = CLIENT_SASL_COMPLETE;
    client_sendf(client, RPL_LOGGEDIN, server->config.server_name,
                 command_reply_nick(client),
                 client->nick[0] != '\0' ? client->nick : "*",
                 client->user[0] != '\0' ? client->user : "*",
                 client->display_host, client->account_name, client->account_name);
    client_sendf(client, RPL_SASLSUCCESS, server->config.server_name,
                 command_reply_nick(client));
    return;

rejected:
    OPENSSL_cleanse(decoded, sizeof(decoded));
    sasl_fail(server, client);
}

CommandResult command_authenticate(Server *server, Client *client, char *params) {
    size_t payload_len;
    int append_result;

    if (server == NULL || client == NULL || params == NULL || *params == '\0')
        return COMMAND_KEEP_CLIENT;
    params[strcspn(params, " ")] = '\0';

    if (client->sasl_state == CLIENT_SASL_COMPLETE) {
        client_sendf(client, ERR_SASLALREADY, server->config.server_name,
                     command_reply_nick(client));
        return COMMAND_KEEP_CLIENT;
    }
    if ((client->capabilities & CLIENT_CAP_SASL) == 0U) {
        sasl_fail(server, client);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcmp(params, "*") == 0) {
        sasl_reset_payload(client);
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
        sasl_reset_payload(client);
        client->sasl_state = CLIENT_SASL_PLAIN_WAIT_DATA;
        client_sendf(client, "AUTHENTICATE +");
        return COMMAND_KEEP_CLIENT;
    }

    if (client->sasl_state != CLIENT_SASL_PLAIN_WAIT_DATA) {
        sasl_fail(server, client);
        return COMMAND_KEEP_CLIENT;
    }

    payload_len = strlen(params);
    if (payload_len > IRCD_SASL_FRAME_MAX) {
        sasl_too_long(server, client);
        return COMMAND_KEEP_CLIENT;
    }

    /* A lone plus is the protocol's zero-length final frame. It is required
     * when the encoded payload length is an exact multiple of 400 bytes. */
    if (strcmp(params, "+") == 0) {
        sasl_finish_plain(server, client);
        return COMMAND_KEEP_CLIENT;
    }

    append_result = sasl_append_frame(client, params, payload_len);
    if (append_result == 0) {
        sasl_too_long(server, client);
        return COMMAND_KEEP_CLIENT;
    }
    if (append_result < 0) {
        sasl_fail(server, client);
        return COMMAND_KEEP_CLIENT;
    }

    if (payload_len < IRCD_SASL_FRAME_MAX)
        sasl_finish_plain(server, client);
    return COMMAND_KEEP_CLIENT;
}
