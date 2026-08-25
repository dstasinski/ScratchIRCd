/**
 * @file message_policy.c
 * @brief Channel color filtering plus +s/+g broadcast helpers.
 */

#include "message_policy.h"
#include "modes.h"
#include "oper.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int is_hex(unsigned char ch) {
    return isxdigit(ch) != 0;
}

static int is_oper_client(const Client *client) {
    return client != NULL &&
           client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN);
}

int message_contains_color(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    if (text == NULL) return 0;
    while (*p != '\0') {
        if (*p == 0x03U || *p == 0x04U || *p == 0x1bU) return 1;
        ++p;
    }
    return 0;
}

static const unsigned char *skip_irc_color(const unsigned char *p) {
    int count = 0;
    ++p;
    while (count < 2 && isdigit(*p)) { ++p; ++count; }
    if (*p == ',') {
        ++p;
        count = 0;
        while (count < 2 && isdigit(*p)) { ++p; ++count; }
    }
    return p;
}

static const unsigned char *skip_irc_hex_color(const unsigned char *p) {
    int count = 0;
    ++p;
    while (count < 6 && is_hex(*p)) { ++p; ++count; }
    if (*p == ',') {
        ++p;
        count = 0;
        while (count < 6 && is_hex(*p)) { ++p; ++count; }
    }
    return p;
}

static const unsigned char *skip_ansi_sgr(const unsigned char *p) {
    const unsigned char *q;
    if (p[0] != 0x1bU || p[1] != '[') return p;
    q = p + 2;
    while ((*q >= '0' && *q <= '9') || *q == ';' || *q == ':') ++q;
    if (*q == 'm') return q + 1;
    return p;
}

void message_strip_color(const char *text, char *out, size_t out_size) {
    const unsigned char *p = (const unsigned char *)text;
    size_t used = 0U;
    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';
    if (text == NULL) return;

    while (*p != '\0' && used + 1U < out_size) {
        const unsigned char *next;
        if (*p == 0x03U) { p = skip_irc_color(p); continue; }
        if (*p == 0x04U) { p = skip_irc_hex_color(p); continue; }
        if (*p == 0x1bU) {
            next = skip_ansi_sgr(p);
            if (next != p) { p = next; continue; }
        }
        out[used++] = (char)*p++;
    }
    out[used] = '\0';
}

void server_notice_broadcast(Server *server, const char *text) {
    size_t i;
    if (server == NULL || text == NULL) return;
    for (i = 0U; i < server->client_count; ++i) {
        Client *target = server->clients[i];
        if (target != NULL && target->registered && is_oper_client(target) &&
            client_mode_has(target->modes, CLIENT_MODE_SERVER_NOTICES)) {
            client_sendf(target, ":%s NOTICE %s :*** %s",
                         server->config.server_name, target->nick, text);
        }
    }
}

void snotice_broadcast(Server *server, SnoticeMask category, const char *fmt, ...) {
    char text[IRCD_OUTPUT_BUFFER_SIZE];
    va_list args;
    size_t i;
    if (server == NULL || category == 0U || fmt == NULL) return;
    va_start(args, fmt);
    (void)vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    text[sizeof(text) - 1U] = '\0';
    for (i = 0U; i < server->client_count; ++i) {
        Client *target = server->clients[i];
        if (target == NULL || !target->registered || !is_oper_client(target)) continue;
        if (!client_mode_has(target->modes, CLIENT_MODE_SERVER_NOTICES)) continue;
        if ((target->snotice_mask & category) == 0U) continue;
        client_sendf(target, ":%s NOTICE %s :*** %s",
                     server->config.server_name, target->nick, text);
    }
}

void oper_message_broadcast(Server *server, const Client *source,
                            const char *command, const char *text) {
    size_t i;
    if (server == NULL || source == NULL || command == NULL || text == NULL) return;
    for (i = 0U; i < server->client_count; ++i) {
        Client *target = server->clients[i];
        if (target != NULL && target->registered && is_oper_client(target) &&
            client_mode_has(target->modes, CLIENT_MODE_GLOBALS)) {
            client_sendf(target, ":%s!%s@%s %s :%s",
                         source->nick, source->user, source->display_host,
                         command, text);
        }
    }
}
