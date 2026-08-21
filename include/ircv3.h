#ifndef IRCD_IRCV3_H
#define IRCD_IRCV3_H

#include "client.h"

/** Notify capable peers sharing a channel that this client's account changed. */
void ircv3_account_notify(Client *client);

#endif /* IRCD_IRCV3_H */
