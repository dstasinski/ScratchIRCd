# Persistent storage growth controls

ScratchIRCd deliberately bounds persistent data that can grow from normal or hostile client activity. Operator-created permanent policy records are treated differently: they remain until an authorized operator removes them.

## IRCv3 channel history

Channel PRIVMSG and NOTICE history is stored in `history_db` and is controlled by two runtime settings:

```text
history_retention_days = 30
history_max_rows = 250000
```

`history_retention_days` removes records older than the configured age. A value of `0` disables age-based expiration. `history_max_rows` is always non-zero and provides the final global row ceiling. History maintenance is throttled so it does not execute for every message. SQLite reuses freed pages; ScratchIRCd does not run `VACUUM` in the message hot path.

## Durable channel-log queue

When ChanServ channel logging is enabled, log events first enter a durable SQLite queue before being appended to text files. The queue is bounded by:

```text
channel_log_queue_max_rows = 250000
```

If the queue reaches this ceiling, ScratchIRCd preserves the existing durable backlog and refuses new log events until queued rows can be flushed. It does not discard the oldest queued records merely to make room. Operators subscribed to the admin/flood SNOTICE categories receive a rate-limited warning while the queue is full.

## Channel log files

Daily files under `logs/` are retained for 90 days. The server checks for expired generated log files at most once per hour and removes regular files whose modification time is older than the retention window. A successful pruning pass that removes files is reported through the admin SNOTICE category.

The retention scan does not follow symlinks and ignores files that do not match the generated `.log.` naming pattern.

## MemoServ

MemoServ growth is bounded by both recipient quota and retention. Defaults are:

```text
memoserv_quota = 100
memoserv_retention_days = 90
```

Because the NickServ account namespace is itself bounded, per-recipient quotas also establish a finite upper bound on the memo namespace.

## NickServ accounts

The NickServ database has a compile-time hard ceiling of 100,000 account rows. Existing accounts remain usable when the ceiling is reached; only creation of additional accounts is refused. Removing an account makes capacity available again.

Password reset and email-verification state are columns on an account row rather than separate append-only token tables, so repeated token requests do not create an independently growing persistent table.

## ChanServ registrations and access

ChanServ has compile-time hard ceilings of:

- 50,000 registered channels globally.
- 256 explicit access entries per registered channel.

At the access-list ceiling, existing entries can still be changed or removed. A new entry can be added after capacity is freed.

The auxiliary `channel_runtime` table is one row per registered channel. Persistent ban, exception, and invite-exception lists inherit the live IRC limit of 100 entries per list. These tables reference the registered channel with `ON DELETE CASCADE`, so dropping a channel registration removes its dependent persistent state.

## KLINE, ZLINE, GeoBAN, and DNSBL

KLINE, ZLINE, and GeoBAN records intentionally permit permanent operator-created policy. ScratchIRCd does not silently age those records out because they represent explicit administrative state.

Automatic DNSBL exact-IP ZLINEs are different. They use the configured `zline_default_duration_seconds` and are timed records, not permanent bans. Expired ban rows are purged by normal ban-database operations, and the DNSBL insertion path also purges expired records before adding a new automatic ZLINE.

## Operational notes

These controls bound row counts and retention windows, but SQLite database files do not necessarily shrink immediately when rows are deleted. SQLite normally reuses freed pages for later writes. Administrators who need to return unused pages to the filesystem can perform an offline or maintenance-window `VACUUM` after making an appropriate backup.

Disk monitoring is still recommended for `data/` and `logs/`, especially when channel logging is enabled on busy channels.