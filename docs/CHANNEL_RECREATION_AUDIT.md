# Channel Recreation Audit

This audit covers the registered-channel recreation path used when a live channel becomes empty, is freed, and is later recreated by a new `JOIN`.

## Audited files

- `src/commands/joinpart.c`
- `src/server.c`
- `src/chanserv.c`
- `src/chanserv_persist.c`
- `include/chanserv.h`
- `include/chanserv_persist.h`

## Verified lifecycle

A live channel is removed when its last member leaves. `server_remove_channel_if_empty()` removes the channel from `server->channels_by_name`, decrements the channel count, and frees the `Channel` object. A later `JOIN` therefore recreates a fresh `Channel` through `server_get_or_create_channel()`.

The `JOIN` path restores ChanServ state before evaluating channel admission policy:

1. Validate the channel name and server/client channel limits.
2. Fetch or create the live `Channel` object.
3. Restore ChanServ registration, MLOCK, topic, parameter modes, and masks.
4. Compute the joining client's ChanServ account privileges.
5. Check key, bans, limits, registered-only, oper/admin-only, secure-only, invite-only, and join throttle policy.
6. Add the client to the channel.
7. Apply service-derived member privileges.
8. Reconcile SecureOps and service privileges.
9. Broadcast JOIN and send topic, greeting, and NAMES numerics.

This ordering means normal restored keys, limits, redirects, bans, exceptions, invite-exceptions, MLOCK state, topic, greeting, and service privileges are in place before JOIN completion is visible to the client.

## Finding: fail-open runtime restore error

`chanserv_restore_channel()` restored the registered-channel record and then called `chanserv_persist_restore()` for runtime state. That runtime restore includes parameter modes and mask lists such as `+k`, `+l`, `+j`, `+L`, `+B`, `+b`, `+e`, and `+I`.

Before this audit, the return value from `chanserv_persist_restore()` was ignored. If SQLite runtime/mask restoration failed, the registered channel could continue through JOIN with incomplete live policy, especially missing bans, exceptions, keys, limits, or redirects.

## Hardening applied

`JOIN` now performs an explicit fail-closed check after ChanServ restoration. If the channel is registered but the runtime persistence layer cannot restore its state, JOIN is denied before key/ban/limit checks or JOIN numerics are sent.

The client receives a server notice explaining that persistent channel state could not be restored. This prevents a corrupted or unreadable runtime/mask persistence layer from silently turning a registered channel permissive.

## Remaining architectural improvement

The long-term cleaner design would be to change `chanserv_restore_channel()` to return success/failure directly and have all callers honor that result. The current fix avoids a broader API change while closing the observable JOIN-side risk.
