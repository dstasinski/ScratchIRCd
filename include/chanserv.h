#ifndef IRCD_CHANSERV_H
#define IRCD_CHANSERV_H

#include "server.h"

/** Handle one command addressed to the virtual ChanServ service. */
void chanserv_handle_message(Server *server, Client *client, char *message);

/** Load a registered channel record into a live Channel object when applicable. */
void chanserv_apply_registration(Server *server, Channel *channel);

/** Return nonzero when the live channel is registered/enabled in ChanServ. */
int chanserv_channel_registered(Server *server, const char *channel_name);

#endif /* IRCD_CHANSERV_H */
