# Milestone 3 Closeout Status

Milestone 3 focuses on MemoServ completion, persistent E-LINE exceptions, configured reserved nicks, operator-visible authentication timestamps, and the pre-release database schema policy.

## Implemented scope

### MemoServ

- Persistent account-to-account memos backed by SQLite.
- User commands for sending, listing, reading, replying, forwarding, deleting inbox entries, deleting sent-history entries, and checking status.
- Command-specific help for the MemoServ user command set.
- Unread memo notification after successful account authentication.
- Recipient-side and sender-side visibility tombstones.
- Physical cleanup after both sides have hidden a memo.
- Account lifecycle handling for disabled and dropped NickServ accounts.
- Network-admin visibility through `MSINFO` and retention cleanup through `MSPURGE`.
- Sender quota behavior aligned with recipient-visible outstanding memos.

### E-LINE exceptions

- Persistent exception storage for KLINE, ZLINE, connection-limit, DNSBL, and GeoBAN policy.
- Exact identity matching and wildcard mask matching.
- Timed and permanent exceptions.
- `/STATS e` visibility for operator review.
- Enforcement coverage for live policy decisions.

### Reserved nicks

- Server-owned service nicknames for `NickServ`, `ChanServ`, and `MemoServ`.
- Configured reserved nickname support.
- Ordinary users are blocked from using configured reserved nicks.
- Operators and network admins may use configured reserved nicks when normal NickServ policy allows it.

### Authentication timestamps

- NickServ stores the last successful account identification timestamp.
- Operator DB stores the last successful `/OPER` timestamp.
- Operator-visible reporting through `STATS N`, `STATS O`, `NSINFO`, and `OPERLIST`.
- UTC text formatting for operator-facing timestamp output.

### Current-schema-only database policy

ScratchIRCd is still pre-release, so development databases are not treated as compatibility contracts. Missing databases are created with the current schema. Incomplete or stale existing databases are rejected rather than silently upgraded in place.

Covered SQLite persistence areas include:

- NickServ accounts.
- Operator accounts.
- MemoServ memos.
- ChanServ channel and access persistence.
- ChanServ logging state and `channel_log_queue`.
- KLINE/ZLINE/E-LINE ban policy persistence.
- GeoBAN persistence.
- IRCv3 channel history persistence.

## Current verification gate

The milestone should not be considered closed until the focused closeout tests pass locally.

```sh
cd ~/Projects/ScratchIRCd
git checkout Genesis
git pull --ff-only origin Genesis
chmod +x tools/test-milestone-3.sh
./tools/test-milestone-3.sh build-m3-gcc focused
```

The focused gate builds and runs the current-schema-only test plus the related database unit tests for ChanServ logging, GeoBAN, history, ban policy, ChanServ, MemoServ, NickServ, and operator persistence.

After that focused set is green, run the broader Milestone 3 integration gate:

```sh
./tools/test-milestone-3.sh build-m3-gcc broad
```

The broad gate builds `scratchircd` and `scratchircd-mkpasswd`, then runs the selected Milestone 3 integration coverage for MemoServ, E-LINE, reserved nicks, NickServ/operator/admin behavior, GeoBAN, history, and ChanServ.

To run both gates in order:

```sh
./tools/test-milestone-3.sh build-m3-gcc all
```

## Static closeout pass

Completed on `Genesis`:

- Client, operator, moderation, and presence documentation are aligned with the current `DEAF`, `MUTE`, and `WATCH` syntax.
- Milestone 3 schema wording is aligned with `docs/DATABASE_SCHEMA_POLICY.md` and no longer requires in-place pre-release schema migration.
- Local verification results have intentionally not been recorded here yet; they remain pending until the focused and broad gates are run by the maintainer.

## Remaining closeout work

- Fix any local build or test fallout from the stricter schema validation.
- Update release notes or the main project milestone summary after the focused and broader gates pass.
- Consider tagging the milestone only after the complete selected test set is green.
