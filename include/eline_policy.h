#ifndef IRCD_ELINE_POLICY_H
#define IRCD_ELINE_POLICY_H

#include "ban_db.h"
#include "runtime_config.h"

/**
 * Return 1 only when an active, valid E-LINE exception of the requested type
 * matches either supplied identity. Return 0 for no match, database errors,
 * invalid rows, or invalid arguments. This intentionally fails closed: callers
 * must continue enforcing the underlying ban or limit unless a clean exception
 * match is found.
 */
static inline int eline_policy_match_path(const char *bans_db,
                                          BanExceptionType type,
                                          const char *identity1,
                                          const char *identity2) {
    BanDb db = {0};
    BanExceptionRecord record;
    int matched = 0;

    if (bans_db == NULL || bans_db[0] == '\0' || identity1 == NULL ||
        identity1[0] == '\0')
        return 0;
    if (ban_db_open(&db, bans_db) != 0)
        return 0;
    matched = ban_exception_db_match(&db, type, identity1, identity2,
                                     &record) == 1;
    ban_db_close(&db);
    return matched;
}

static inline int eline_policy_match_server(const ServerConfig *config,
                                            BanExceptionType type,
                                            const char *identity1,
                                            const char *identity2) {
    return config != NULL && eline_policy_match_path(config->bans_db, type,
                                                     identity1, identity2);
}

#endif
