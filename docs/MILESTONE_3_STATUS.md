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

cmake -S . -B build-m3-gcc

cmake --build build-m3-gcc \
  --target test_current_schema_only test_chanserv_logging_db \
           test_geoban_db test_history_db test_ban_db \
           test_chanserv_db test_memoserv_db test_nickserv_db test_operator_db \
  -j"$(nproc)"

ctest --test-dir build-m3-gcc \
  -R 'current_schema_only|chanserv_logging_database|geoban_database|history_database|ban_database|chanserv_database|memoserv_database|nickserv_database|operator_database' \
  --output-on-failure
```

After that focused set is green, run the broader Milestone 3 integration gate:

```sh
cmake --build build-m3-gcc --target scratchircd scratchircd-mkpasswd -j"$(nproc)"

ctest --test-dir build-m3-gcc \
  -R 'memoserv|eline|reserved_nick|nickserv|operator|oper|geoban|history|chanserv' \
  --output-on-failure
```

## Remaining closeout work

- Fix any local build or test fallout from the stricter schema validation.
- Do one final static pass for stale migration or legacy-schema wording.
- Update release notes or the main project milestone summary after the focused and broader gates pass.
- Consider tagging the milestone only after the complete selected test set is green.
