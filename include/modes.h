#ifndef IRCD_MODES_H
#define IRCD_MODES_H

/**
 * @file modes.h
 * @brief Core representation of ScratchIRCd user, channel, and membership modes.
 *
 * This header defines storage and query primitives only. Command-specific
 * permission checks and MODE parsing belong in the command/policy layer.
 */

#include <stdint.h>
#include <stddef.h>

typedef uint64_t ClientModeSet;

#define CLIENT_MODE_BOT              (UINT64_C(1) << 0)
#define CLIENT_MODE_DEAF             (UINT64_C(1) << 1)
#define CLIENT_MODE_GLOBALS          (UINT64_C(1) << 2)
#define CLIENT_MODE_HIDE_OPER        (UINT64_C(1) << 3)
#define CLIENT_MODE_HELPOP           (UINT64_C(1) << 4)
#define CLIENT_MODE_HIDE_IDLE        (UINT64_C(1) << 5)
#define CLIENT_MODE_INVISIBLE        (UINT64_C(1) << 6)
#define CLIENT_MODE_NETADMIN         (UINT64_C(1) << 7)
#define CLIENT_MODE_OPER             (UINT64_C(1) << 8)
#define CLIENT_MODE_PRIVATE          (UINT64_C(1) << 9)
#define CLIENT_MODE_REGONLY_MSG      (UINT64_C(1) << 10)
#define CLIENT_MODE_REGISTERED       (UINT64_C(1) << 11)
#define CLIENT_MODE_SERVICE          (UINT64_C(1) << 12)
#define CLIENT_MODE_SERVER_NOTICES   (UINT64_C(1) << 13)
#define CLIENT_MODE_NO_CTCP          (UINT64_C(1) << 14)
#define CLIENT_MODE_VHOST            (UINT64_C(1) << 15)
#define CLIENT_MODE_WEBIRC           (UINT64_C(1) << 16)
#define CLIENT_MODE_WHOIS_NOTICE     (UINT64_C(1) << 17)
#define CLIENT_MODE_WALLOPS          (UINT64_C(1) << 18)
#define CLIENT_MODE_CLOAKED          (UINT64_C(1) << 19)
#define CLIENT_MODE_SECURE           (UINT64_C(1) << 20)

typedef uint64_t ChannelModeSet;

#define CHANNEL_MODE_ADMIN_ONLY      (UINT64_C(1) << 0)
#define CHANNEL_MODE_NO_COLOR        (UINT64_C(1) << 1)
#define CHANNEL_MODE_INVITE_ONLY     (UINT64_C(1) << 2)
#define CHANNEL_MODE_NO_KNOCK        (UINT64_C(1) << 3)
#define CHANNEL_MODE_REGONLY_SPEAK   (UINT64_C(1) << 4)
#define CHANNEL_MODE_MODERATED       (UINT64_C(1) << 5)
#define CHANNEL_MODE_NO_EXTERNAL     (UINT64_C(1) << 6)
#define CHANNEL_MODE_OPER_ONLY       (UINT64_C(1) << 7)
#define CHANNEL_MODE_PRIVATE         (UINT64_C(1) << 8)
#define CHANNEL_MODE_REGISTERED      (UINT64_C(1) << 9)
#define CHANNEL_MODE_REGONLY_JOIN    (UINT64_C(1) << 10)
#define CHANNEL_MODE_STRIP_COLOR     (UINT64_C(1) << 11)
#define CHANNEL_MODE_SECRET          (UINT64_C(1) << 12)
#define CHANNEL_MODE_TOPIC_LOCK      (UINT64_C(1) << 13)
#define CHANNEL_MODE_NO_NOTICE       (UINT64_C(1) << 14)
#define CHANNEL_MODE_NO_INVITE       (UINT64_C(1) << 15)
#define CHANNEL_MODE_SECURE_ONLY     (UINT64_C(1) << 16)

typedef uint32_t ChannelPrivilegeSet;

#define CHANNEL_PRIV_VOICE           (UINT32_C(1) << 0)
#define CHANNEL_PRIV_HALFOP          (UINT32_C(1) << 1)
#define CHANNEL_PRIV_OPERATOR        (UINT32_C(1) << 2)
#define CHANNEL_PRIV_OWNER           (UINT32_C(1) << 3)

int client_mode_has(ClientModeSet modes, ClientModeSet mask);
ClientModeSet client_mode_add(ClientModeSet modes, ClientModeSet mask);
ClientModeSet client_mode_remove(ClientModeSet modes, ClientModeSet mask);
int channel_mode_has(ChannelModeSet modes, ChannelModeSet mask);
ChannelModeSet channel_mode_add(ChannelModeSet modes, ChannelModeSet mask);
ChannelModeSet channel_mode_remove(ChannelModeSet modes, ChannelModeSet mask);
int channel_privilege_has(ChannelPrivilegeSet privileges,
                          ChannelPrivilegeSet mask);
char channel_privilege_prefix(ChannelPrivilegeSet privileges);
size_t channel_privilege_format(ChannelPrivilegeSet privileges,
                                char *buffer, size_t buffer_size);

/**
 * Return the highest channel privilege rank.
 * 0 = none, 1 = voice, 2 = halfop, 3 = operator, 4 = owner.
 */
unsigned int channel_privilege_rank(ChannelPrivilegeSet privileges);

#endif /* IRCD_MODES_H */
