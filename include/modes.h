#ifndef IRCD_MODES_H
#define IRCD_MODES_H

/**
 * @file modes.h
 * @brief Core representation of ScratchIRCd user, channel, and membership modes.
 *
 * This header defines storage and query primitives only.  Command-specific
 * permission checks and MODE parsing belong in the command/policy layer.  By
 * keeping representation independent from parsing, WHO/WHOIS, services,
 * ChanServ, NAMES, and operator commands can all consume the same mode state.
 */

#include <stdint.h>
#include <stddef.h>

/**
 * Client (user) mode bitset.
 *
 * Every user mode in the ScratchIRCd specification has a dedicated bit even
 * when its behavior will be implemented in a later command milestone.
 */
typedef uint64_t ClientModeSet;

#define CLIENT_MODE_BOT              (UINT64_C(1) << 0)  /**< +B bot marker. */
#define CLIENT_MODE_DEAF             (UINT64_C(1) << 1)  /**< +d suppress normal channel PRIVMSG. */
#define CLIENT_MODE_GLOBALS          (UINT64_C(1) << 2)  /**< +g receive/send globops/locops. */
#define CLIENT_MODE_HIDE_OPER        (UINT64_C(1) << 3)  /**< +H hide IRCop status. */
#define CLIENT_MODE_HELPOP           (UINT64_C(1) << 4)  /**< +h available for help. */
#define CLIENT_MODE_HIDE_IDLE        (UINT64_C(1) << 5)  /**< +I hide oper idle time. */
#define CLIENT_MODE_INVISIBLE        (UINT64_C(1) << 6)  /**< +i invisible to ordinary WHO. */
#define CLIENT_MODE_NETADMIN         (UINT64_C(1) << 7)  /**< +N network administrator. */
#define CLIENT_MODE_OPER             (UINT64_C(1) << 8)  /**< +o IRC operator. */
#define CLIENT_MODE_PRIVATE          (UINT64_C(1) << 9)  /**< +p hide channels in WHOIS. */
#define CLIENT_MODE_REGONLY_MSG      (UINT64_C(1) << 10) /**< +R accept messages only from +r. */
#define CLIENT_MODE_REGISTERED       (UINT64_C(1) << 11) /**< +r identified registered nickname. */
#define CLIENT_MODE_SERVICE          (UINT64_C(1) << 12) /**< +S protected service identity. */
#define CLIENT_MODE_SERVER_NOTICES   (UINT64_C(1) << 13) /**< +s receive server notices. */
#define CLIENT_MODE_NO_CTCP          (UINT64_C(1) << 14) /**< +T suppress CTCPs. */
#define CLIENT_MODE_VHOST            (UINT64_C(1) << 15) /**< +t using a virtual host. */
#define CLIENT_MODE_WEBIRC           (UINT64_C(1) << 16) /**< +V authenticated WEBIRC client. */
#define CLIENT_MODE_WHOIS_NOTICE     (UINT64_C(1) << 17) /**< +W notice WHOIS queries (opers). */
#define CLIENT_MODE_WALLOPS          (UINT64_C(1) << 18) /**< +w receive WALLOPS. */
#define CLIENT_MODE_CLOAKED          (UINT64_C(1) << 19) /**< +x hidden/cloaked hostname. */
#define CLIENT_MODE_SECURE           (UINT64_C(1) << 20) /**< +z TLS-secured connection. */

/** Channel-wide boolean mode bitset. Parameter/list modes have extra fields. */
typedef uint64_t ChannelModeSet;

#define CHANNEL_MODE_ADMIN_ONLY      (UINT64_C(1) << 0)  /**< +A administrators only. */
#define CHANNEL_MODE_NO_COLOR        (UINT64_C(1) << 1)  /**< +c reject ANSI color. */
#define CHANNEL_MODE_INVITE_ONLY     (UINT64_C(1) << 2)  /**< +i invite required. */
#define CHANNEL_MODE_NO_KNOCK        (UINT64_C(1) << 3)  /**< +K KNOCK forbidden. */
#define CHANNEL_MODE_REGONLY_SPEAK   (UINT64_C(1) << 4)  /**< +M +r required to speak. */
#define CHANNEL_MODE_MODERATED       (UINT64_C(1) << 5)  /**< +m voiced or privileged to speak. */
#define CHANNEL_MODE_NO_EXTERNAL     (UINT64_C(1) << 6)  /**< +n no outside messages. */
#define CHANNEL_MODE_OPER_ONLY       (UINT64_C(1) << 7)  /**< +O IRC operators only. */
#define CHANNEL_MODE_PRIVATE         (UINT64_C(1) << 8)  /**< +p private channel. */
#define CHANNEL_MODE_REGISTERED      (UINT64_C(1) << 9)  /**< +r ChanServ-registered channel. */
#define CHANNEL_MODE_REGONLY_JOIN    (UINT64_C(1) << 10) /**< +R +r required to join. */
#define CHANNEL_MODE_STRIP_COLOR     (UINT64_C(1) << 11) /**< +S strip incoming color. */
#define CHANNEL_MODE_SECRET          (UINT64_C(1) << 12) /**< +s secret channel. */
#define CHANNEL_MODE_TOPIC_LOCK      (UINT64_C(1) << 13) /**< +t halfop or above sets topic. */
#define CHANNEL_MODE_NO_NOTICE       (UINT64_C(1) << 14) /**< +T NOTICE forbidden. */
#define CHANNEL_MODE_NO_INVITE       (UINT64_C(1) << 15) /**< +V INVITE forbidden. */
#define CHANNEL_MODE_SECURE_ONLY     (UINT64_C(1) << 16) /**< +z TLS clients only. */

/**
 * Per-membership privilege bits.
 *
 * These belong to the relationship between one client and one channel, not to
 * either object globally.  A client can therefore be owner in one channel,
 * voiced in another, and unprivileged in a third.
 */
typedef uint32_t ChannelPrivilegeSet;

#define CHANNEL_PRIV_VOICE           (UINT32_C(1) << 0) /**< +v, NAMES prefix '+'. */
#define CHANNEL_PRIV_HALFOP          (UINT32_C(1) << 1) /**< +h, NAMES prefix '%'. */
#define CHANNEL_PRIV_OPERATOR        (UINT32_C(1) << 2) /**< +o, NAMES prefix '@'. */
#define CHANNEL_PRIV_OWNER           (UINT32_C(1) << 3) /**< +q, NAMES prefix '~'. */

/** Return non-zero when any bit in mask is set in modes. */
int client_mode_has(ClientModeSet modes, ClientModeSet mask);

/** Set all bits in mask and return the resulting client mode set. */
ClientModeSet client_mode_add(ClientModeSet modes, ClientModeSet mask);

/** Clear all bits in mask and return the resulting client mode set. */
ClientModeSet client_mode_remove(ClientModeSet modes, ClientModeSet mask);

/** Return non-zero when any bit in mask is set in channel modes. */
int channel_mode_has(ChannelModeSet modes, ChannelModeSet mask);

/** Set all bits in mask and return the resulting channel mode set. */
ChannelModeSet channel_mode_add(ChannelModeSet modes, ChannelModeSet mask);

/** Clear all bits in mask and return the resulting channel mode set. */
ChannelModeSet channel_mode_remove(ChannelModeSet modes, ChannelModeSet mask);

/** Return non-zero when membership has at least one privilege in mask. */
int channel_privilege_has(ChannelPrivilegeSet privileges,
                          ChannelPrivilegeSet mask);

/** Return the highest visible NAMES prefix for a membership, or '\0'. */
char channel_privilege_prefix(ChannelPrivilegeSet privileges);

/**
 * Format privilege letters in descending rank (qohv).
 * Returns the number of letters written, excluding the terminating NUL.
 */
size_t channel_privilege_format(ChannelPrivilegeSet privileges,
                                char *buffer, size_t buffer_size);

#endif /* IRCD_MODES_H */
