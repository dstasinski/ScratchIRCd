#ifndef IRCD_OPER_H
#define IRCD_OPER_H

/**
 * @file oper.h
 * @brief IRC operator privilege and selective server-notice representation.
 */

#include <stdint.h>
#include <stddef.h>

typedef uint64_t OperPermissionSet;

#define OPER_PERMISSION_REHASH    (UINT64_C(1) << 0)
#define OPER_PERMISSION_DIE       (UINT64_C(1) << 1)
#define OPER_PERMISSION_RESTART   (UINT64_C(1) << 2)
#define OPER_PERMISSION_WALLOPS   (UINT64_C(1) << 3)
#define OPER_PERMISSION_KILL      (UINT64_C(1) << 4)
#define OPER_PERMISSION_KLINE     (UINT64_C(1) << 5)
#define OPER_PERMISSION_UNKLINE   (UINT64_C(1) << 6)
#define OPER_PERMISSION_ZLINE     (UINT64_C(1) << 7)
#define OPER_PERMISSION_OVERRIDE  (UINT64_C(1) << 8)
#define OPER_PERMISSION_GETHOST   (UINT64_C(1) << 9)
#define OPER_PERMISSION_HELPOP    (UINT64_C(1) << 10)
#define OPER_PERMISSION_NETADMIN  (UINT64_C(1) << 11)
#define OPER_PERMISSION_GEOBAN    (UINT64_C(1) << 12)

#define OPER_PERMISSION_ALL \
    (OPER_PERMISSION_REHASH | OPER_PERMISSION_DIE | OPER_PERMISSION_RESTART | \
     OPER_PERMISSION_WALLOPS | OPER_PERMISSION_KILL | OPER_PERMISSION_KLINE | \
     OPER_PERMISSION_UNKLINE | OPER_PERMISSION_ZLINE | OPER_PERMISSION_OVERRIDE | \
     OPER_PERMISSION_GETHOST | OPER_PERMISSION_HELPOP | OPER_PERMISSION_NETADMIN | \
     OPER_PERMISSION_GEOBAN)

/* Selective server-notice categories. Letters are the public SNOTICE mask. */
typedef uint32_t SnoticeMask;
#define SNOTICE_CONNECTIONS   (UINT32_C(1) << 0)  /* c */
#define SNOTICE_OPERATORS     (UINT32_C(1) << 1)  /* o */
#define SNOTICE_KILLS         (UINT32_C(1) << 2)  /* k */
#define SNOTICE_BANS          (UINT32_C(1) << 3)  /* b */
#define SNOTICE_GEOBANS       (UINT32_C(1) << 4)  /* g */
#define SNOTICE_WEBIRC        (UINT32_C(1) << 5)  /* w */
#define SNOTICE_DNS           (UINT32_C(1) << 6)  /* d */
#define SNOTICE_SECURITY      (UINT32_C(1) << 7)  /* s */
#define SNOTICE_ADMIN         (UINT32_C(1) << 8)  /* a */
#define SNOTICE_SERVICES      (UINT32_C(1) << 9)  /* v */
#define SNOTICE_REGISTRATIONS (UINT32_C(1) << 10) /* r */
#define SNOTICE_IDENTITY      (UINT32_C(1) << 11) /* x */
#define SNOTICE_MODERATION    (UINT32_C(1) << 12) /* m */
#define SNOTICE_FLOOD         (UINT32_C(1) << 13) /* f */
#define SNOTICE_ALL (SNOTICE_CONNECTIONS | SNOTICE_OPERATORS | SNOTICE_KILLS | \
                     SNOTICE_BANS | SNOTICE_GEOBANS | SNOTICE_WEBIRC | \
                     SNOTICE_DNS | SNOTICE_SECURITY | SNOTICE_ADMIN | \
                     SNOTICE_SERVICES | SNOTICE_REGISTRATIONS | \
                     SNOTICE_IDENTITY | SNOTICE_MODERATION | SNOTICE_FLOOD)

struct Server;

int oper_permission_has(OperPermissionSet permissions, OperPermissionSet mask);
int oper_permissions_parse(const char *text, OperPermissionSet *permissions);
size_t oper_permissions_format(OperPermissionSet permissions,
                               char *buffer, size_t buffer_size);
SnoticeMask snotice_mask_for_letter(char letter);
size_t snotice_mask_format(SnoticeMask mask, char *buffer, size_t buffer_size);
void snotice_broadcast(struct Server *server, SnoticeMask category,
                       const char *fmt, ...);

#endif /* IRCD_OPER_H */
