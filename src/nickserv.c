/**
 * @file nickserv.c
 * @brief Virtual NickServ implementation.
 *
 * NickServ is not a Client: it is never inserted into client hashes, channel
 * membership, NAMES, WHO, LIST, ISON or LUSERS. PRIVMSG NickServ and the
 * direct NICKSERV command both route into this implementation.
 */

#include "nickserv.h"
#include "mail.h"
#include "modes.h"
#include "nickserv_db.h"

#include <argon2.h>
#include <ctype.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <sys/types.h>
#include <time.h>

static void nickserv_notice(Server *server, Client *client, const char *text) {
    client_sendf(client, ":NickServ!service@%s NOTICE %s :%s",
                 server->config.server_name, client->nick, text);
}

static int hash_password(const char *password, char *encoded, size_t encoded_size) {
    uint8_t salt[IRCD_ARGON2_SALT_BYTES];
    size_t offset = 0U;

    while (offset < sizeof(salt)) {
        ssize_t got = getrandom(salt + offset, sizeof(salt) - offset, 0);
        if (got <= 0) return -1;
        offset += (size_t)got;
    }

    return argon2id_hash_encoded(IRCD_ARGON2_TIME_COST,
                                 IRCD_ARGON2_MEMORY_COST_KIB,
                                 IRCD_ARGON2_PARALLELISM,
                                 password, strlen(password),
                                 salt, sizeof(salt),
                                 IRCD_ARGON2_HASH_BYTES,
                                 encoded, encoded_size) == ARGON2_OK ? 0 : -1;
}

/** Generate a 128-bit random token represented as lowercase hexadecimal. */
static int generate_token(char *token, size_t token_size) {
    unsigned char bytes[IRCD_RESET_TOKEN_BYTES];
    size_t i;
    if (token == NULL || token_size < IRCD_RESET_TOKEN_HEX_LEN + 1U) return -1;
    if (RAND_bytes(bytes, (int)sizeof(bytes)) != 1) return -1;
    for (i = 0U; i < sizeof(bytes); ++i)
        (void)snprintf(token + i * 2U, token_size - i * 2U, "%02x", bytes[i]);
    token[IRCD_RESET_TOKEN_HEX_LEN] = '\0';
    return 0;
}

/** Hash a plaintext token so reset/verification secrets are never persisted. */
static void hash_token(const char *token, char *out, size_t out_size) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    size_t i;
    if (out == NULL || out_size < IRCD_TOKEN_HASH_HEX_LEN + 1U) return;
    SHA256((const unsigned char *)token, strlen(token), digest);
    for (i = 0U; i < sizeof(digest); ++i)
        (void)snprintf(out + i * 2U, out_size - i * 2U, "%02x", digest[i]);
    out[IRCD_TOKEN_HASH_HEX_LEN] = '\0';
}

static int valid_email(const char *address) {
    const char *at;
    const char *dot;
    const unsigned char *p;
    size_t length;
    if (address == NULL) return 0;
    length = strlen(address);
    if (length < 3U || length > IRCD_EMAIL_MAX) return 0;
    for (p = (const unsigned char *)address; *p != '\0'; ++p)
        if (*p <= 32U || *p == 127U || *p == '\r' || *p == '\n') return 0;
    at = strchr(address, '@');
    if (at == NULL || at == address || strchr(at + 1, '@') != NULL || at[1] == '\0') return 0;
    dot = strrchr(at + 1, '.');
    return dot != NULL && dot != at + 1 && dot[1] != '\0';
}

static int mail_available(const Server *server) {
    return server->config.sendmail_path[0] != '\0' && server->config.mail_from[0] != '\0';
}

static int send_token_mail(Server *server, const char *to, const char *account,
                           const char *token, int verification) {
    MailRequest request;
    unsigned int lifetime = verification ? server->config.nickserv_verify_seconds
                                         : server->config.nickserv_reset_seconds;
    if (!mail_available(server)) return -1;
    memset(&request, 0, sizeof(request));
    (void)snprintf(request.to, sizeof(request.to), "%s", to);
    (void)snprintf(request.from, sizeof(request.from), "%s", server->config.mail_from);
    (void)snprintf(request.subject, sizeof(request.subject), "%s NickServ %s",
                   server->config.network_name,
                   verification ? "email verification" : "password reset");
    if (verification) {
        (void)snprintf(request.body, sizeof(request.body),
                       "A request was made to verify this email address for NickServ account %s on %s.\n\n"
                       "Verification token: %s\n\n"
                       "On IRC, use: /NICKSERV VERIFY %s\n\n"
                       "This token expires in %u seconds. If you did not request this, ignore this message.",
                       account, server->config.network_name, token, token, lifetime);
    } else {
        (void)snprintf(request.body, sizeof(request.body),
                       "A password reset was requested for NickServ account %s on %s.\n\n"
                       "Reset token: %s\n\n"
                       "On IRC, use: /NICKSERV RESET %s %s <new-password>\n\n"
                       "This token expires in %u seconds and can be used only once. If you did not request this, ignore this message.",
                       account, server->config.network_name, token, account, token, lifetime);
    }
    return mail_send_async(server->config.sendmail_path, &request);
}

/** Apply authenticated account state without changing real_ip/real_host. */
static void apply_account(Client *client, const NickServAccount *account) {
    (void)snprintf(client->account_name, sizeof(client->account_name), "%s", account->name);
    client->modes = client_mode_add(client->modes, CLIENT_MODE_REGISTERED);

    if (account->vhost[0] != '\0') {
        (void)snprintf(client->display_host, sizeof(client->display_host), "%s", account->vhost);
        client->modes = client_mode_remove(client->modes, CLIENT_MODE_CLOAKED);
        client->modes = client_mode_add(client->modes, CLIENT_MODE_VHOST);
    }
}

int nickserv_identify(Server *server, Client *client,
                      const char *account_name, const char *password) {
    NickServDb db = {0};
    NickServAccount account;
    int found;

    if (server == NULL || client == NULL || account_name == NULL || password == NULL ||
        *account_name == '\0' || *password == '\0') return 0;
    if (client->account_name[0] != '\0' &&
        strcasecmp(client->account_name, account_name) != 0) return 0;

    if (nickserv_db_open(&db, server->config.nickserv_db) != 0) return 0;
    found = nickserv_db_get(&db, account_name, &account);
    nickserv_db_close(&db);
    if (found != 1 || !account.enabled) return 0;
    if (argon2id_verify(account.password_hash, password, strlen(password)) != ARGON2_OK) return 0;

    apply_account(client, &account);
    return 1;
}

static void command_register(Server *server, Client *client, char *password) {
    NickServDb db = {0};
    NickServAccount account;
    int existing;

    if (password == NULL || *password == '\0') {
        nickserv_notice(server, client, "Syntax: REGISTER <password>");
        return;
    }
    if (client->nick[0] == '\0') {
        nickserv_notice(server, client, "You must have a nickname before registering.");
        return;
    }
    if (client->account_name[0] != '\0') {
        nickserv_notice(server, client, "You are already identified to an account.");
        return;
    }

    if (nickserv_db_open(&db, server->config.nickserv_db) != 0) {
        nickserv_notice(server, client, "Account database is unavailable.");
        return;
    }
    existing = nickserv_db_get(&db, client->nick, &account);
    if (existing != 0) {
        nickserv_db_close(&db);
        nickserv_notice(server, client, existing == 1
            ? "That nickname is already registered."
            : "Account lookup failed.");
        return;
    }

    memset(&account, 0, sizeof(account));
    (void)snprintf(account.name, sizeof(account.name), "%s", client->nick);
    account.enabled = 1;
    if (hash_password(password, account.password_hash, sizeof(account.password_hash)) != 0 ||
        nickserv_db_add(&db, &account) != 0) {
        nickserv_db_close(&db);
        nickserv_notice(server, client, "Nickname registration failed.");
        return;
    }
    nickserv_db_close(&db);
    apply_account(client, &account);
    nickserv_notice(server, client, "Nickname registered and identified.");
}

static void command_identify(Server *server, Client *client, char *params) {
    char *first = params != NULL ? strtok(params, " ") : NULL;
    char *second = first != NULL ? strtok(NULL, "") : NULL;
    const char *account;
    const char *password;

    if (first == NULL) {
        nickserv_notice(server, client, "Syntax: IDENTIFY [nick] <password>");
        return;
    }
    if (second == NULL) {
        account = client->nick;
        password = first;
    } else {
        while (*second == ' ') ++second;
        account = first;
        password = second;
    }

    nickserv_notice(server, client,
        nickserv_identify(server, client, account, password)
            ? "Password accepted - you are now identified."
            : "Password incorrect or account unavailable.");
}

static void command_set_password(Server *server, Client *client, char *password) {
    NickServDb db = {0};
    char encoded[IRCD_OPER_HASH_MAX + 1U];

    if (client->account_name[0] == '\0') {
        nickserv_notice(server, client, "You must identify before changing your password.");
        return;
    }
    if (password == NULL || *password == '\0') {
        nickserv_notice(server, client, "Syntax: SET PASSWORD <new-password>");
        return;
    }
    if (hash_password(password, encoded, sizeof(encoded)) != 0 ||
        nickserv_db_open(&db, server->config.nickserv_db) != 0 ||
        nickserv_db_set_password(&db, client->account_name, encoded) != 0) {
        nickserv_db_close(&db);
        nickserv_notice(server, client, "Password change failed.");
        return;
    }
    nickserv_db_close(&db);
    nickserv_notice(server, client, "Password changed.");
}

static void command_set_email(Server *server, Client *client, char *address) {
    NickServDb db = {0};
    char token[IRCD_RESET_TOKEN_HEX_LEN + 1U];
    char token_hash[IRCD_TOKEN_HASH_HEX_LEN + 1U];
    long long expires_at;

    if (client->account_name[0] == '\0') {
        nickserv_notice(server, client, "You must identify before setting an email address.");
        return;
    }
    if (!valid_email(address)) {
        nickserv_notice(server, client, "Syntax: SET EMAIL <address>");
        return;
    }
    if (!mail_available(server)) {
        nickserv_notice(server, client, "Email services are not configured on this server.");
        return;
    }
    if (generate_token(token, sizeof(token)) != 0) {
        nickserv_notice(server, client, "Unable to create verification token.");
        return;
    }
    hash_token(token, token_hash, sizeof(token_hash));
    expires_at = (long long)time(NULL) + (long long)server->config.nickserv_verify_seconds;

    if (nickserv_db_open(&db, server->config.nickserv_db) != 0 ||
        nickserv_db_set_email_challenge(&db, client->account_name, address,
                                        token_hash, expires_at) != 0) {
        nickserv_db_close(&db);
        nickserv_notice(server, client, "Unable to save email verification request.");
        return;
    }
    nickserv_db_close(&db);
    if (send_token_mail(server, address, client->account_name, token, 1) != 0) {
        nickserv_notice(server, client, "Unable to queue verification email.");
        return;
    }
    nickserv_notice(server, client, "Verification email queued. Use NICKSERV VERIFY <token> when it arrives.");
}

static void command_verify(Server *server, Client *client, char *token) {
    NickServDb db = {0};
    char token_hash[IRCD_TOKEN_HASH_HEX_LEN + 1U];
    int rc;

    if (client->account_name[0] == '\0') {
        nickserv_notice(server, client, "You must identify before verifying an email address.");
        return;
    }
    if (token == NULL || *token == '\0') {
        nickserv_notice(server, client, "Syntax: VERIFY <token>");
        return;
    }
    hash_token(token, token_hash, sizeof(token_hash));
    if (nickserv_db_open(&db, server->config.nickserv_db) != 0) {
        nickserv_notice(server, client, "Account database is unavailable.");
        return;
    }
    rc = nickserv_db_verify_email(&db, client->account_name, token_hash, (long long)time(NULL));
    nickserv_db_close(&db);
    nickserv_notice(server, client, rc == 1
        ? "Email address verified."
        : "Verification token is invalid or expired.");
}

/** Change a connected squatter's nickname while preserving its session. */
static int recover_rename(Server *server, Client *target) {
    char old_nick[IRC_NICK_MAX + 1U];
    char guest_nick[IRC_NICK_MAX + 1U];
    char message[IRCD_MESSAGE_BUFFER_SIZE];
    ClientChannelLink *link;

    (void)snprintf(old_nick, sizeof(old_nick), "%s", target->nick);
    (void)snprintf(guest_nick, sizeof(guest_nick), "Guest%llu",
                   (unsigned long long)target->id);
    if (hash_get(&server->clients_by_nick, guest_nick) != NULL) return -1;

    (void)hash_remove(&server->clients_by_nick, old_nick);
    (void)snprintf(target->nick, sizeof(target->nick), "%s", guest_nick);
    if (hash_set(&server->clients_by_nick, target->nick, target) != 0) {
        (void)snprintf(target->nick, sizeof(target->nick), "%s", old_nick);
        (void)hash_set(&server->clients_by_nick, target->nick, target);
        return -1;
    }

    (void)snprintf(message, sizeof(message), ":%s!%s@%s NICK :%s\r\n",
                   old_nick, target->user, target->display_host, target->nick);
    for (link = target->channels; link != NULL; link = link->next)
        channel_broadcast(link->channel, NULL, message);
    client_sendf(target, ":NickServ!service@%s NOTICE %s :Your nickname was recovered by its registered owner; you are now %s.",
                 server->config.server_name, target->nick, target->nick);
    return 0;
}

static int recover_authorized(Server *server, Client *client, const char *nick) {
    NickServDb db = {0};
    NickServAccount account;
    int found;
    if (client->account_name[0] == '\0' || nick == NULL ||
        strcasecmp(client->account_name, nick) != 0) return 0;
    if (nickserv_db_open(&db, server->config.nickserv_db) != 0) return 0;
    found = nickserv_db_get(&db, nick, &account);
    nickserv_db_close(&db);
    return found == 1 && account.enabled;
}

static void command_recover(Server *server, Client *client, char *params, int ghost_alias) {
    char *nick = params != NULL ? strtok(params, " ") : NULL;
    char *action = nick != NULL ? strtok(NULL, " ") : NULL;
    Client *target;
    int kill_target = ghost_alias || (action != NULL && strcasecmp(action, "KILL") == 0);

    if (nick == NULL || (!ghost_alias && action != NULL && !kill_target)) {
        nickserv_notice(server, client,
            ghost_alias ? "Syntax: GHOST <nick>" : "Syntax: RECOVER <nick> [KILL]");
        return;
    }
    if (!recover_authorized(server, client, nick)) {
        nickserv_notice(server, client, "You must be identified to that registered account.");
        return;
    }
    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) {
        nickserv_notice(server, client, "That nickname is not currently in use.");
        return;
    }
    if (target == client) {
        nickserv_notice(server, client, "You are already using that nickname.");
        return;
    }

    if (kill_target) {
        char reason[IRC_QUIT_REASON_MAX + 1U];
        (void)snprintf(reason, sizeof(reason), "NickServ %s by %s",
                       ghost_alias ? "GHOST" : "RECOVER", client->account_name);
        server_disconnect(server, target, reason);
        nickserv_notice(server, client, "Nickname session disconnected.");
    } else if (recover_rename(server, target) == 0) {
        nickserv_notice(server, client, "Nickname recovered; the previous user was safely renamed.");
    } else {
        nickserv_notice(server, client, "Unable to recover that nickname.");
    }
}

static void command_reset(Server *server, Client *client, char *params) {
    char *account_name = params != NULL ? strtok(params, " ") : NULL;
    char *token = account_name != NULL ? strtok(NULL, " ") : NULL;
    char *new_password = token != NULL ? strtok(NULL, "") : NULL;

    if (account_name == NULL) {
        nickserv_notice(server, client,
            "Syntax: RESET <nick>  OR  RESET <nick> <token> <new-password>");
        return;
    }

    if (token == NULL) {
        NickServDb db = {0};
        NickServAccount account;
        char plain_token[IRCD_RESET_TOKEN_HEX_LEN + 1U];
        char token_hash[IRCD_TOKEN_HASH_HEX_LEN + 1U];
        long long expires_at;
        int found;

        /* Always return the same public response to avoid account/email enumeration. */
        if (mail_available(server) &&
            nickserv_db_open(&db, server->config.nickserv_db) == 0) {
            found = nickserv_db_get(&db, account_name, &account);
            if (found == 1 && account.enabled && account.email_verified &&
                account.email[0] != '\0' && generate_token(plain_token, sizeof(plain_token)) == 0) {
                hash_token(plain_token, token_hash, sizeof(token_hash));
                expires_at = (long long)time(NULL) + (long long)server->config.nickserv_reset_seconds;
                if (nickserv_db_set_reset_token(&db, account.name, token_hash, expires_at) == 0)
                    (void)send_token_mail(server, account.email, account.name, plain_token, 0);
            }
            nickserv_db_close(&db);
        }
        nickserv_notice(server, client,
            "If that account exists and has a verified email address, a password reset message has been queued.");
        return;
    }

    if (new_password == NULL || *new_password == '\0') {
        nickserv_notice(server, client, "Syntax: RESET <nick> <token> <new-password>");
        return;
    }
    while (*new_password == ' ') ++new_password;
    {
        NickServDb db = {0};
        char token_hash[IRCD_TOKEN_HASH_HEX_LEN + 1U];
        char password_hash[IRCD_OPER_HASH_MAX + 1U];
        int rc = -1;
        hash_token(token, token_hash, sizeof(token_hash));
        if (hash_password(new_password, password_hash, sizeof(password_hash)) == 0 &&
            nickserv_db_open(&db, server->config.nickserv_db) == 0) {
            rc = nickserv_db_consume_reset_token(&db, account_name, token_hash,
                                                 (long long)time(NULL), password_hash);
            nickserv_db_close(&db);
        }
        nickserv_notice(server, client, rc == 1
            ? "Password reset complete. You may now IDENTIFY with the new password."
            : "Reset token is invalid or expired.");
    }
}

void nickserv_handle_message(Server *server, Client *client, char *text) {
    char *command;
    char *rest;

    if (server == NULL || client == NULL || text == NULL) return;
    command = strtok(text, " ");
    rest = strtok(NULL, "");
    if (rest != NULL) while (*rest == ' ') ++rest;

    if (command == NULL) return;
    if (strcasecmp(command, "REGISTER") == 0) {
        command_register(server, client, rest);
    } else if (strcasecmp(command, "IDENTIFY") == 0) {
        command_identify(server, client, rest);
    } else if (strcasecmp(command, "RECOVER") == 0) {
        command_recover(server, client, rest, 0);
    } else if (strcasecmp(command, "GHOST") == 0) {
        command_recover(server, client, rest, 1);
    } else if (strcasecmp(command, "RESET") == 0) {
        command_reset(server, client, rest);
    } else if (strcasecmp(command, "VERIFY") == 0) {
        command_verify(server, client, rest);
    } else if (strcasecmp(command, "SET") == 0) {
        char *field = rest != NULL ? strtok(rest, " ") : NULL;
        char *value = field != NULL ? strtok(NULL, "") : NULL;
        if (value != NULL) while (*value == ' ') ++value;
        if (field != NULL && value != NULL && strcasecmp(field, "PASSWORD") == 0)
            command_set_password(server, client, value);
        else if (field != NULL && value != NULL && strcasecmp(field, "EMAIL") == 0)
            command_set_email(server, client, value);
        else
            nickserv_notice(server, client,
                "Syntax: SET PASSWORD <new-password>  OR  SET EMAIL <address>");
    } else if (strcasecmp(command, "HELP") == 0) {
        nickserv_notice(server, client,
            "Commands: REGISTER, IDENTIFY, RECOVER, GHOST, SET PASSWORD, SET EMAIL, VERIFY, RESET");
    } else {
        nickserv_notice(server, client, "Unknown command. Use HELP.");
    }
}

int service_nickname_reserved(const char *nick) {
    if (nick == NULL) return 0;
    return strcasecmp(nick, "NickServ") == 0 ||
           strcasecmp(nick, "ChanServ") == 0 ||
           strcasecmp(nick, "MemoServ") == 0;
}
