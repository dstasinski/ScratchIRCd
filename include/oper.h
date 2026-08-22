#ifndef IRCD_OPER_H
#define IRCD_OPER_H

/**
 * @file oper.h
 * @brief IRC operator privilege representation and parsing helpers.
 *
 * Operator authority is independent from user mode +o.  Ordinary operator
 * permissions are loaded from operators.db; the bootstrap network
 * administrator receives the complete permission set after authenticating
 * against ircd.conf.
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

int oper_permission_has(OperPermissionSet permissions, OperPermissionSet mask);
int oper_permissions_parse(const char *text, OperPermissionSet *permissions);
size_t oper_permissions_format(OperPermissionSet permissions,
                               char *buffer, size_t buffer_size);

#endif /* IRCD_OPER_H */
