#ifndef IRCD_CLOAK_H
#define IRCD_CLOAK_H

#include <stddef.h>

struct Client;
struct ServerConfig;

int cloak_generate(const struct ServerConfig *config,
                   const char *real_ip, const char *real_host,
                   char *out, size_t out_size);
void cloak_refresh_display_host(const struct ServerConfig *config,
                                struct Client *client);

#endif
