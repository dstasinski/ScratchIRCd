#ifndef IRCD_IRC_H
#define IRCD_IRC_H

#include "server.h"

/**
 * Parse and execute one CRLF-stripped IRC protocol line.
 *
 * The input buffer is modified in place during tokenization.  A return value
 * of 1 requests that the caller disconnect the client (QUIT); 0 keeps it.
 */
int irc_handle_line(Server *server, Client *client, char *line);

#endif /* IRCD_IRC_H */
