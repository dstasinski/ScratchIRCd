/** @file test_oper.c @brief Unit tests for operator permission parsing. */

#include "oper.h"

#include <assert.h>
#include <string.h>

int main(void) {
    OperPermissionSet permissions = 0U;
    char formatted[256];

    assert(oper_permissions_parse(
               "oper,can_rehash,can_kill,can_kline,can_unkline,can_zline,"
               "can_override,get_host,helpop,can_wallops,netadmin",
               &permissions) == 0);
    assert(oper_permission_has(permissions, OPER_PERMISSION_REHASH));
    assert(oper_permission_has(permissions, OPER_PERMISSION_KILL));
    assert(oper_permission_has(permissions, OPER_PERMISSION_KLINE));
    assert(oper_permission_has(permissions, OPER_PERMISSION_UNKLINE));
    assert(oper_permission_has(permissions, OPER_PERMISSION_ZLINE));
    assert(oper_permission_has(permissions, OPER_PERMISSION_OVERRIDE));
    assert(oper_permission_has(permissions, OPER_PERMISSION_GETHOST));
    assert(oper_permission_has(permissions, OPER_PERMISSION_HELPOP));
    assert(oper_permission_has(permissions, OPER_PERMISSION_WALLOPS));
    assert(oper_permission_has(permissions, OPER_PERMISSION_NETADMIN));
    assert(!oper_permission_has(permissions, OPER_PERMISSION_DIE));

    (void)oper_permissions_format(permissions, formatted, sizeof(formatted));
    assert(strstr(formatted, "can_kill") != NULL);
    assert(strstr(formatted, "netadmin") != NULL);

    assert(oper_permissions_parse("can_kill,not_a_flag", &permissions) == -1);

    /* The bootstrap netadmin mask must include every defined permission. */
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_REHASH));
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_DIE));
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_RESTART));
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_WALLOPS));
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_KILL));
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_KLINE));
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_UNKLINE));
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_ZLINE));
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_OVERRIDE));
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_GETHOST));
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_HELPOP));
    assert(oper_permission_has(OPER_PERMISSION_ALL, OPER_PERMISSION_NETADMIN));
    return 0;
}
