#ifndef IRCD_CHANSERV_H
#define IRCD_CHANSERV_H

#include "server.h"

void chanserv_handle_message(Server *server, Client *client, char *text);
int chanserv_channel_lookup(Server *server, const char *name,
                            char *founder, size_t founder_size);

#endif
