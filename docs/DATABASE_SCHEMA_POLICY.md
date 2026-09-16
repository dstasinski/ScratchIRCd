# ScratchIRCd Database Schema Policy

ScratchIRCd has not had a public release yet. Until the first release, persistent SQLite schemas are treated as current-development schemas, not compatibility contracts.

## Current rule

Development databases may be deleted and recreated when schemas change. The daemon should create the current schema for a missing database and reject an incompatible existing schema rather than trying to repair or migrate an old development database in place.

This applies to the current service and policy databases, including:

- `nickserv.db`
- `operators.db`
- `memoserv.db`
- ChanServ channel/access persistence
- ChanServ logging state and `channel_log_queue`
- KLINE/ZLINE/E-LINE ban policy persistence
- GeoBAN persistence
- IRCv3 channel history persistence
- other SQLite-backed policy or service stores while they remain pre-release

The focused `current_schema_only` unit test covers incomplete existing schemas for the main SQLite stores so stale development databases fail loudly instead of being silently upgraded.

## Why this rule exists

Pre-release migrations add maintenance cost and can hide broken test fixtures or stale local databases. During active development, it is safer to make schema changes explicit, keep the current schema clear, and fail closed when an existing database does not match expected columns or field constraints.

## What to do when a schema changes

For local development and test systems, stop the daemon and delete the affected database under the configured data directory. Restarting the daemon will recreate the database using the current schema.

Example:

```sh
rm -f data/nickserv.db data/operators.db data/memoserv.db
rm -f data/chanserv.db data/bans.db data/geoban.db data/history.db
```

Only delete databases in a disposable development environment. Back up anything you want to inspect before removal.

## Verification

After touching SQLite schema creation or validation code, run the focused Milestone 3 closeout gate:

```sh
chmod +x tools/test-milestone-3.sh
./tools/test-milestone-3.sh build-m3-gcc focused
```

Use `./tools/test-milestone-3.sh --help` for the helper's usage. The focused gate builds and runs `current_schema_only` plus the related database unit tests for NickServ, operators, MemoServ, ChanServ, ChanServ logging, ban policy, GeoBAN, and history.

For a narrower check while iterating on only the combined schema rejection target, the minimal command is:

```sh
cmake --build build-m3-gcc --target test_current_schema_only -j"$(nproc)"
ctest --test-dir build-m3-gcc -R '^current_schema_only$' --output-on-failure
```

For changes that touch an individual database module, also run that module's focused unit test, such as `nickserv_database`, `operator_database`, `memoserv_database`, `chanserv_database`, `chanserv_logging_database`, `ban_database`, `geoban_database`, or `history_database`.

## After the first release

Once ScratchIRCd ships a release that users may run persistently, schema changes should be handled by explicit, tested migrations. At that point, this policy should be replaced with a release-aware migration policy that documents supported upgrade paths, `PRAGMA user_version` changes, failure modes, and rollback expectations.
