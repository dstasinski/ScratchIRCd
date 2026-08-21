#ifndef IRCD_MEMOSERV_H
#define IRCD_MEMOSERV_H

/**
 * @file memoserv.h
 * @brief Virtual MemoServ service for persistent account-to-account messages.
 */

#include "server.h"

/** Process text addressed to the virtual MemoServ service. */
void memoserv_handle_message(Server *server, Client *client, char *text);

/** Notify an authenticated client of unread memo count without revealing content. */
void memoserv_notify_unread(Server *server, Client *client);

#endif /* IRCD_MEMOSERV_H */
