#ifndef IRCD_OPER_H
#define IRCD_OPER_H

/**
 * @file oper.h
 * @brief IRC operator privilege representation and parsing helpers.
 *
 * Operator authority is intentionally represented independently from user
 * modes. User mode +o says that a client is an IRC operator; this bitset says
 * what that operator is actually authorized to do. The same representation
 * can later be populated from NickServ/SQLite account flags instead of the
 * bootstrap ircd.conf OPER block.
 */

#include <stdint.h>
#include <stddef.h>

/** Complete operator privilege bitset. */
typedef uint64_t OperPermissionSet;

#define OPER_PERMISSION_REHASH    (UINT64_C(1) << 0)  /**< can_rehash */
#define OPER_PERMISSION_DIE       (UINT64_C(1) << 1)  /**< can_die */
#define OPER_PERMISSION_RESTART   (UINT64_C(1) << 2)  /**< can_restart */
#define OPER_PERMISSION_WALLOPS   (UINT64_C(1) << 3)  /**< can_wallops */
#define OPER_PERMISSION_KILL      (UINT64_C(1) << 4)  /**< can_kill */
#define OPER_PERMISSION_KLINE     (UINT64_C(1) << 5)  /**< can_kline */
#define OPER_PERMISSION_UNKLINE   (UINT64_C(1) << 6)  /**< can_unkline */
#define OPER_PERMISSION_ZLINE     (UINT64_C(1) << 7)  /**< can_zline */
#define OPER_PERMISSION_OVERRIDE  (UINT64_C(1) << 8)  /**< can_override */
#define OPER_PERMISSION_GETHOST   (UINT64_C(1) << 9)  /**< get_host */
#define OPER_PERMISSION_HELPOP    (UINT64_C(1) << 10) /**< helpop */
#define OPER_PERMISSION_NETADMIN  (UINT64_C(1) << 11) /**< netadmin */

/** Return non-zero when any bit in mask is present. */
int oper_permission_has(OperPermissionSet permissions, OperPermissionSet mask);

/**
 * Parse a comma-separated privilege list into a bitset.
 *
 * Supported names match the registered-nickname flag names in the ScratchIRCd
 * specification. "oper" is accepted as a marker but does not consume a bit;
 * successful OPER authentication itself supplies user mode +o.
 * Returns 0 on success or -1 when an unknown token is present.
 */
int oper_permissions_parse(const char *text, OperPermissionSet *permissions);

/** Format permissions as a stable comma-separated string for diagnostics. */
size_t oper_permissions_format(OperPermissionSet permissions,
                               char *buffer, size_t buffer_size);

#endif /* IRCD_OPER_H */
