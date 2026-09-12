# ScratchIRCd Database Schema Policy

ScratchIRCd has not had a public release yet. Until the first release, persistent SQLite schemas are treated as current-development schemas, not compatibility contracts.

## Current rule

Development databases may be deleted and recreated when schemas change. The daemon should create the current schema for a missing database and reject an incompatible existing schema rather than trying to repair or migrate an old development database in place.

This applies to the current service and policy databases, including:

- `nickserv.db`
- `operators.db`
- `memoserv.db`
- other SQLite-backed policy or service stores while they remain pre-release

## Why this rule exists

Pre-release migrations add maintenance cost and can hide broken test fixtures or stale local databases. During active development, it is safer to make schema changes explicit, keep the current schema clear, and fail closed when an existing database does not match expected columns or field constraints.

## What to do when a schema changes

For local development and test systems, stop the daemon and delete the affected database under the configured data directory. Restarting the daemon will recreate the database using the current schema.

Example:

```sh
rm -f data/nickserv.db data/operators.db data/memoserv.db
```

Only delete databases in a disposable development environment. Back up anything you want to inspect before removal.

## After the first release

Once ScratchIRCd ships a release that users may run persistently, schema changes should be handled by explicit, tested migrations. At that point, this policy should be replaced with a release-aware migration policy that documents supported upgrade paths, `PRAGMA user_version` changes, failure modes, and rollback expectations.
