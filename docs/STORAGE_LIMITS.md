# Persistent storage and service resource controls

ScratchIRCd deliberately bounds persistent data and scarce server resources that can grow from normal or hostile client activity. Operator-created permanent policy records are treated differently: they remain until an authorized operator removes them.

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

MemoServ growth is bounded by recipient quota, sender fair-share quota, and retention. Defaults are:

```text
memoserv_quota = 100
memoserv_sender_quota = 500
memoserv_retention_days = 90
```

The recipient quota prevents any one mailbox from growing indefinitely. The sender quota limits the number of retained memos attributed to one authenticated sender across all recipients, preventing one account from consuming a disproportionate part of the global memo store. Recipient deletion and retention expiry return sender capacity. An index on `(sender,id)` keeps sender-quota checks bounded and inexpensive.

Because the NickServ account namespace is itself bounded, these per-account limits also establish a finite upper bound on the memo namespace.

## NickServ accounts

The NickServ database has a compile-time hard ceiling of 100,000 account rows. Existing accounts remain usable when the ceiling is reached; only creation of additional accounts is refused. Removing an account makes capacity available again.

Account creation also has an ephemeral per-real-IP fair-share throttle:

```text
nickserv_registrations_per_ip = 5
nickserv_registration_window_seconds = 3600
```

A value of `0` for `nickserv_registrations_per_ip` disables this fair-share throttle, but it does not disable the global account-row ceiling. The throttle uses the final end-user `real_ip`, including WebIRC clients after trusted gateway processing. It is maintained in a fixed-size in-memory table and registration IPs are not written to NickServ SQLite storage.

Password reset and email-verification state are columns on an account row rather than separate append-only token tables, so repeated token requests do not create an independently growing persistent table.

## NickServ email delivery

Mail-producing `SET EMAIL` and request-form `RESET <account>` operations are protected separately from ordinary IRC command flood limits:

```text
nickserv_mail_requests_per_ip = 5
nickserv_mail_window_seconds = 900
nickserv_mail_global_per_minute = 60
```

The per-IP limit prevents one source from repeatedly generating verification/reset mail, while the global per-minute ceiling bounds detached sendmail-worker creation even when requests come from many different addresses. The per-IP state is bounded and memory-only. Token verification and the token-consuming password-reset form do not generate mail and therefore do not consume this budget.

## ChanServ registrations and access

ChanServ has compile-time hard ceilings of:

- 50,000 registered channels globally.
- 256 explicit access entries per registered channel.

A founder additionally has a runtime fair-share ceiling:

```text
chanserv_max_channels_per_account = 20
```

A value of `0` disables the per-founder fair-share limit but not the 50,000-row global ceiling. Lowering the setting does not remove existing registrations; it only prevents that founder from creating additional registrations until usage falls below the configured limit. Network administrators may bypass the founder fair-share limit for recovery and administration.

At the access-list ceiling, existing entries can still be changed or removed. A new entry can be added after capacity is freed.

The auxiliary `channel_runtime` table is one row per registered channel. Persistent ban, exception, and invite-exception lists inherit the live IRC limit of 100 entries per list. These tables reference the registered channel with `ON DELETE CASCADE`, so dropping a channel registration removes its dependent persistent state.

## DNS and DNSBL helper queues

FCrDNS and DNSBL work is submitted through nonblocking OS pipes to dedicated workers. Pipe capacity therefore provides a finite queue: when submission is saturated, the server does not allocate an unbounded user-space backlog. DNS result pipes are also nonblocking, and registration deadlines recover from missing or delayed results according to the configured policy.

## KLINE, ZLINE, GeoBan, and DNSBL

KLINE, ZLINE, and GeoBan records intentionally permit permanent operator-created policy. ScratchIRCd does not silently age those records out because they represent explicit administrative state.

Automatic DNSBL exact-IP ZLINEs are different. They use the configured `zline_default_duration_seconds` and are timed records, not permanent bans. Expired ban rows are purged by normal ban-database operations, and the DNSBL insertion path also purges expired records before adding a new automatic ZLINE.

## REHASH behavior

The fair-share values above are runtime policy and may be changed by REHASH. Lowering a quota does not delete existing accounts, registered channels, or memos. It changes whether future allocations or mail-producing requests are accepted. Persistent database paths and other startup-bound identity/resource settings retain their existing RESTART requirements.

## Operational notes

These controls bound row counts, retention windows, and selected helper-resource creation, but SQLite database files do not necessarily shrink immediately when rows are deleted. SQLite normally reuses freed pages for later writes. Administrators who need to return unused pages to the filesystem can perform an offline or maintenance-window `VACUUM` after making an appropriate backup.

Disk, process-count, and filesystem monitoring is still recommended for `data/` and `logs/`, especially when channel logging or NickServ email delivery is enabled.
