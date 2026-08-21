#ifndef IRCD_CHANSERV_H
#define IRCD_CHANSERV_H

#include "server.h"

/** Process a command addressed to the virtual ChanServ service. */
void chanserv_handle_message(Server *server, Client *client, char *text);

/** Restore service-controlled registration, mode-lock, topic, and runtime state. */
void chanserv_restore_channel(Server *server, Channel *channel);

/** True when this authenticated client owns the enabled channel registration. */
int chanserv_client_is_founder(Server *server, const Client *client, const char *channel_name);

/** Return persistent ChanServ membership privileges for an authenticated client. */
ChannelPrivilegeSet chanserv_client_privileges(Server *server, const Client *client,
                                               const char *channel_name);

/**
 * Return non-zero when a boolean MODE change is compatible with the stored
 * ChanServ MLOCK. Unregistered channels are always allowed.
 */
int chanserv_mode_change_allowed(Server *server, const Channel *channel,
                                 ChannelModeSet bit, int adding);

/** Persist +k/+l/+j/+L/+B and +b/+e/+I state for a registered channel. */
void chanserv_persist_channel(Server *server, const Channel *channel);

#endif
