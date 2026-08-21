#ifndef IRCD_CHANSERV_H
#define IRCD_CHANSERV_H

#include "server.h"

/** Process a command addressed to the virtual ChanServ service. */
void chanserv_handle_message(Server *server, Client *client, char *text);

/** Restore service-controlled registration, mode-lock, and topic state. */
void chanserv_restore_channel(Server *server, Channel *channel);

/** True when this authenticated client owns the enabled channel registration. */
int chanserv_client_is_founder(Server *server, const Client *client, const char *channel_name);

/** Return persistent ChanServ membership privileges for an authenticated client. */
ChannelPrivilegeSet chanserv_client_privileges(Server *server, const Client *client,
                                               const char *channel_name);

#endif
