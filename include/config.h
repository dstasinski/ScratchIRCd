#ifndef IRCD_CONFIG_H
#define IRCD_CONFIG_H

/*
 * config.h
 *
 * Compile-time limits and safe defaults for ScratchIRCd.
 *
 * Settings that administrators should be able to change without recompiling
 * belong in the runtime ServerConfig structure (runtime_config.h). This file
 * defines storage sizes, hard limits, protocol constants, and defaults.
 */

/* -------------------------------------------------------------------------
 * Server defaults and hard limits
 * ------------------------------------------------------------------------- */

#define IRCD_DEFAULT_SERVER_NAME "scratch.local"
#define IRCD_DEFAULT_NETWORK_NAME "ScratchNet"
#define IRCD_VERSION "ScratchIRCd-0.4"
#define IRCD_CREATED "August 2026"
#define IRCD_DEFAULT_PORT "6667"
#define IRCD_DEFAULT_BIND_ADDRESS ""
#define IRCD_DEFAULT_MAX_CLIENTS 1024U
#define IRCD_HARD_MAX_CLIENTS 65535U
#define IRCD_LISTEN_BACKLOG 128
#define IRCD_MAX_LISTENERS 16U
#define IRCD_CLIENT_HASH_BUCKETS 4093U
#define IRCD_CHANNEL_HASH_BUCKETS 4093U
#define IRCD_OUTPUT_BUFFER_SIZE 1024U
#define IRCD_MESSAGE_BUFFER_SIZE 1024U
#define IRCD_NETWORK_NAME_MAX 63U
#define IRCD_PORT_TEXT_MAX 15U
#define IRCD_CONFIG_LINE_MAX 1024U
#define IRCD_DEFAULT_CONFIG_FILE "ircd.conf"
#define IRCD_DEFAULT_DNS_TIMEOUT_SECONDS 5U

/*
 * These strings remain intentionally conservative. MODE state exists, but
 * RPL_AVAILABLE/ISUPPORT should advertise only behavior actually enforced
 * end-to-end by the command and policy layers.
 */
#define IRCD_SUPPORTED_USER_MODES ""
#define IRCD_SUPPORTED_CHANNEL_MODES ""
#define IRCD_ISUPPORT_BASE \
    "CASEMAPPING=rfc1459 CHANTYPES=#& NICKLEN=31 CHANNELLEN=63"

#define IRCD_SERVER_NAME IRCD_DEFAULT_SERVER_NAME
#define IRCD_NETWORK_NAME IRCD_DEFAULT_NETWORK_NAME
#define IRCD_ISUPPORT IRCD_ISUPPORT_BASE

/* -------------------------------------------------------------------------
 * Client limits and defaults
 * ------------------------------------------------------------------------- */

#define IRC_NICK_MAX 31U
#define IRC_USER_MAX 31U
#define IRC_REALNAME_MAX 127U
#define IRC_HOST_MAX 255U
#define IRC_IP_MAX 45U
#define IRC_INPUT_BUFFER_SIZE 4096U
#define IRC_MAX_CHANNELS_PER_CLIENT 32U
#define IRC_UNKNOWN_HOST "unknown"
#define IRC_DEFAULT_QUIT_REASON "Client quit"
#define IRC_QUIT_REASON_MAX 255U
#define IRC_AWAY_MAX 255U
#define IRCD_SHUTDOWN_REASON "Server shutting down"
#define IRC_CANNOT_SEND_NOT_MEMBER_TEXT "not on channel"
#define IRC_DEFAULT_PART_REASON "Leaving"
#define IRC_MODE_MAX_PARAMS 32U

/* -------------------------------------------------------------------------
 * Channel limits and protocol constants
 * ------------------------------------------------------------------------- */

#define IRC_CHANNEL_NAME_MAX 63U
#define IRC_CHANNEL_KEY_MAX 63U
#define IRC_CHANNEL_MASK_MAX 255U
#define IRC_CHANNEL_TOPIC_MAX 390U
#define IRC_CHANNEL_TOPIC_SETTER_MAX (IRC_NICK_MAX + IRC_USER_MAX + IRC_HOST_MAX + 3U)
#define IRC_CHANNEL_PREFIXES "#&"
#define IRC_CHANNEL_PREFIX '#'
#define IRC_NAMES_PUBLIC_MARKER '='
#define IRC_NAMES_PRIVATE_MARKER '*'
#define IRC_NAMES_BUFFER_SIZE 768U
#define IRC_DEFAULT_KICK_REASON "Kicked"
#define IRC_KICK_REASON_MAX 255U

/** Maximum chained +L/+B redirects followed for one JOIN request. */
#define IRC_JOIN_REDIRECT_MAX 4U

#endif /* IRCD_CONFIG_H */
