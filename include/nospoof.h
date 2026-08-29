#ifndef IRCD_NOSPOOF_H
#define IRCD_NOSPOOF_H

#include "server.h"

/* Start the connection PING-cookie and CTCP VERSION probes once a nick exists. */
void nospoof_start(Server *server, Client *client);

/* Request WebIRC-specific client website metadata once WEBIRC is authenticated. */
void nospoof_request_website(Server *server, Client *client);

/* Handle a client PONG. Returns nonzero when it consumed the cookie response. */
int nospoof_handle_pong(Server *server, Client *client, const char *params);

/* Capture CTCP VERSION/WEBSITE NOTICE replies addressed to the server. */
int nospoof_handle_notice(Server *server, Client *client, const char *params);

/* True from VERSION probe transmission until a valid reply is captured. */
int nospoof_version_restricted(const Server *server, const Client *client);
/* During that window, allow messages only to an IRC operator/netadmin. */
int nospoof_version_target_allowed(const Server *server, const Client *client,
                                   const char *target);

#endif
