#ifndef IRCD_CONFIG_H
#define IRCD_CONFIG_H

/*
 * config.h
 *
 * Compile-time configuration for the IRC daemon.
 *
 * The project deliberately keeps tunable server, client, and channel values
 * in one place.  The protocol and data-structure headers consume these
 * definitions rather than embedding policy-specific constants of their own.
 */

/* -------------------------------------------------------------------------
 * Server configuration
 * ------------------------------------------------------------------------- */

/** Human-readable IRC server name used as the prefix of server messages. */
#define IRCD_SERVER_NAME "simple.local"

/** Network name presented to newly registered clients in RPL_WELCOME. */
#define IRCD_NETWORK_NAME "ScratchNet"

/** Program/version string advertised by RPL_YOURHOST and RPL_AVAILABLE. */
#define IRCD_VERSION "simple-ircd-0.3"

/** Build/creation description advertised by RPL_CREATED. */
#define IRCD_CREATED "August 2026"

/** Default TCP service used when no command-line port is supplied. */
#define IRCD_DEFAULT_PORT "6667"

/**
 * Default bind address.  NULL means all local interfaces through AI_PASSIVE.
 * Change to a literal such as "127.0.0.1" to restrict the listener.
 */
#define IRCD_DEFAULT_BIND_ADDRESS NULL

/** Maximum number of simultaneously connected clients. */
#define IRCD_MAX_CLIENTS 128U

/** Number of buckets in the case-insensitive nickname hash table. */
#define IRCD_CLIENT_HASH_BUCKETS 257U

/** Number of buckets in the case-insensitive channel-name hash table. */
#define IRCD_CHANNEL_HASH_BUCKETS 257U

/** Kernel listen backlog requested for the IRC listening socket. */
#define IRCD_LISTEN_BACKLOG 32

/** Maximum size of a formatted outbound IRC line, including CRLF. */
#define IRCD_OUTPUT_BUFFER_SIZE 1024U

/** Maximum size used when constructing broadcast messages. */
#define IRCD_MESSAGE_BUFFER_SIZE 1024U

/** User modes advertised in RPL_AVAILABLE.  None are implemented yet. */
#define IRCD_SUPPORTED_USER_MODES ""

/** Channel modes advertised in RPL_AVAILABLE.  None are implemented yet. */
#define IRCD_SUPPORTED_CHANNEL_MODES ""

/** ISUPPORT tokens advertised through numeric 005. */
#define IRCD_ISUPPORT "CASEMAPPING=ascii CHANTYPES=# NICKLEN=31 CHANNELLEN=63"

/* -------------------------------------------------------------------------
 * Client configuration
 * ------------------------------------------------------------------------- */

/** Maximum nickname length accepted by this first iteration. */
#define IRC_NICK_MAX 31U

/** Maximum USER/ident field length stored per client. */
#define IRC_USER_MAX 31U

/** Maximum real-name (gecos) length stored per client. */
#define IRC_REALNAME_MAX 127U

/** Maximum textual hostname/address length stored per client. */
#define IRC_HOST_MAX 63U

/** Receive buffer retained for incomplete IRC input lines. */
#define IRC_INPUT_BUFFER_SIZE 4096U

/** Maximum number of channels a single client may join. */
#define IRC_MAX_CHANNELS_PER_CLIENT 32U

/** Fallback hostname text when an address cannot be rendered. */
#define IRC_UNKNOWN_HOST "unknown"

/** Default QUIT reason when a client supplies none. */
#define IRC_DEFAULT_QUIT_REASON "Client quit"

/** Maximum stored text length for a client-supplied QUIT reason. */
#define IRC_QUIT_REASON_MAX 255U

/** Reason used when the server itself disconnects all clients on shutdown. */
#define IRCD_SHUTDOWN_REASON "Server shutting down"

/** Detail inserted into ERR_CANNOTSENDTOCHAN for a non-member sender. */
#define IRC_CANNOT_SEND_NOT_MEMBER_TEXT "not on channel"

/** Default PART reason when a client supplies none. */
#define IRC_DEFAULT_PART_REASON "Leaving"

/* -------------------------------------------------------------------------
 * Channel configuration
 * ------------------------------------------------------------------------- */

/** Maximum stored channel-name length. */
#define IRC_CHANNEL_NAME_MAX 63U

/** Only this prefix is recognized as a channel prefix in this iteration. */
#define IRC_CHANNEL_PREFIX '#'

/** Channel membership marker used by RPL_NAMREPLY for public channels. */
#define IRC_NAMES_PUBLIC_MARKER '='

/** Temporary buffer used to assemble a NAMES reply. */
#define IRC_NAMES_BUFFER_SIZE 768U

#endif /* IRCD_CONFIG_H */
