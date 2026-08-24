#ifndef IRCD_CHANNEL_LOG_H
#define IRCD_CHANNEL_LOG_H

#include "server.h"

/**
 * Optional ChanServ-controlled per-channel text logging.
 *
 * Logs contain only JOIN/PART/QUIT lifecycle events and channel
 * PRIVMSG/NOTICE traffic.  Mode/topic/kick and other protocol activity is
 * intentionally excluded.
 */
void channel_log_join(Server *server, Channel *channel, Client *client);
void channel_log_part(Server *server, Channel *channel, Client *client,
                      const char *reason);
void channel_log_quit(Server *server, Channel *channel, Client *client,
                      const char *reason);
void channel_log_message(Server *server, Channel *channel, Client *client,
                         const char *text, int is_notice);

/** Rotate known enabled logs when the local calendar day changes. */
void channel_log_rotate_all(time_t now);

/**
 * Handle CHANSERV SET <channel> LOGGING ON|OFF.
 * Returns non-zero when the text was recognized and fully handled.
 */
int channel_log_handle_chanserv(Server *server, Client *client,
                                const char *text);

#endif /* IRCD_CHANNEL_LOG_H */
