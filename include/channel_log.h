#ifndef IRCD_CHANNEL_LOG_H
#define IRCD_CHANNEL_LOG_H

#include "server.h"

#define IRCD_CHANNEL_LOG_PATH_MAX 1024U
#ifndef IRCD_PATH_MAX
#define IRCD_PATH_MAX IRCD_CHANNEL_LOG_PATH_MAX
#endif
#define IRCD_CHANNEL_LOG_BATCH_SECONDS 300

/** Initialize/migrate the durable channel-log queue at server startup. */
int channel_log_init(Server *server);

/**
 * Optional ChanServ-controlled per-channel text logging.
 *
 * Events are first persisted to a durable SQLite queue and are then appended
 * to text log files in approximately five-minute batches. Logs contain only
 * JOIN/PART/QUIT lifecycle events and channel PRIVMSG/NOTICE traffic.
 */
void channel_log_join(Server *server, Channel *channel, Client *client);
void channel_log_part(Server *server, Channel *channel, Client *client,
                      const char *reason);
void channel_log_quit(Server *server, Channel *channel, Client *client,
                      const char *reason);
void channel_log_message(Server *server, Channel *channel, Client *client,
                         const char *text, int is_notice);

/** Flush one bounded batch whose oldest records have waited at least five minutes. */
void channel_log_flush_due(Server *server, time_t now);

/** Rotate/flush known logs from the existing server event-loop clock. */
void channel_log_rotate_all(time_t now);

/**
 * Handle CHANSERV SET <channel> LOGGING ON|OFF.
 * Returns non-zero when the text was recognized and fully handled.
 */
int channel_log_handle_chanserv(Server *server, Client *client,
                                const char *text);

#endif /* IRCD_CHANNEL_LOG_H */
