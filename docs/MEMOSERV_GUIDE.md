# ScratchIRCd MemoServ Guide

MemoServ is ScratchIRCd's persistent account-to-account memo service. It is a virtual server service, not a normal IRC client. It never joins channels and never appears in ordinary NAMES, WHO, ISON, LIST, or LUSERS output.

Memo ownership is tied to authenticated NickServ account names, not to temporary nicknames. A user must be connected, registered, and identified to a NickServ account before using MemoServ commands that read, send, reply, forward, delete, or report mailbox status.

## Configuration

MemoServ uses these server configuration values:

```text
memoserv_db = data/memoserv.db
memoserv_quota = 100
memoserv_retention_days = 90
```

`memoserv_db` is the SQLite database path. `memoserv_quota` is the maximum number of visible inbox memos for one recipient account. `memoserv_retention_days` controls automatic expiration by memo creation time. A value of `0` disables automatic expiration.

Each memo stores a generated numeric ID, sender account, recipient account, message text, creation time, read time, sender-side sent-history visibility, and recipient-side inbox visibility. Reading a memo marks it read, but it remains stored until both user-facing sides hide it, retention expiry, or account cleanup.

MemoServ can automatically migrate the known legacy table shape that predates per-side memo visibility. The migration adds sender and recipient visibility fields plus supporting visible-sent and visible-inbox indexes while keeping existing sent and received rows visible. If an existing `memos` table is missing required legacy columns, MemoServ rejects that database instead of guessing how to repair it. Operators should restore a valid backup or migrate the table manually before pointing `memoserv_db` at it again.

## Authentication

All current MemoServ user commands require an authenticated NickServ account. If the user is not identified, MemoServ replies:

```text
You must identify to NickServ before using MemoServ.
```

The sender stored with a memo is the authenticated account name. Nick changes do not alter memo ownership.

After successful direct IDENTIFY, NickServ IDENTIFY, or SASL identification, MemoServ reports the unread count when it is nonzero. Memo contents are never displayed automatically.

## User commands

```text
/MEMOSERV SEND <account> :<message>
/MEMOSERV LIST
/MEMOSERV SENT
/MEMOSERV READ <memo-id>
/MEMOSERV REPLY <memo-id> :<message>
/MEMOSERV FORWARD <memo-id> <account>
/MEMOSERV DEL <memo-id>
/MEMOSERV DEL ALL
/MEMOSERV DELETE <memo-id>
/MEMOSERV DELETE ALL
/MEMOSERV DELSENT <memo-id>
/MEMOSERV DELSENT ALL
/MEMOSERV STATUS
/MEMOSERV HELP
/MEMOSERV HELP <command>
```

The traditional service-message form is also supported:

```irc
/PRIVMSG MemoServ :SEND <account> :<message>
```

## SEND

Send a memo to an enabled NickServ account:

```irc
/MEMOSERV SEND Daniel :Please check the channel policy when you get back.
```

The destination must be an existing enabled NickServ account. If the account does not exist or is disabled, MemoServ replies:

```text
That NickServ account does not exist or is disabled.
```

If the recipient mailbox has reached `memoserv_quota`, MemoServ replies:

```text
Recipient memo box is full.
```

If the text exceeds the configured MemoServ text limit, MemoServ replies:

```text
Memo text is too long.
```

When delivery succeeds, MemoServ replies with the generated memo ID:

```text
Memo #12 sent to Daniel.
```

If the recipient account is online and identified, MemoServ also sends that user a short notice:

```text
New memo #12 from Alice. Use /MEMOSERV READ 12 to read it.
```

## LIST and SENT

Show received memos:

```irc
/MEMOSERV LIST
```

A received memo row currently looks like this:

```text
#12 UNREAD from Alice at 2026-09-11T13:45:00Z
```

Show sent memos:

```irc
/MEMOSERV SENT
```

Unread sent memo rows show when the memo was sent:

```text
#13 TO Bob UNREAD sent 2026-09-11T13:46:00Z
```

Read sent memo rows show both the sent time and the recipient read time:

```text
#13 TO Bob READ sent 2026-09-11T13:46:00Z read 2026-09-11T14:02:00Z
```

`LIST` and `SENT` show at most the configured internal list limit for one reply set. User-facing memo timestamps are formatted as UTC text using `YYYY-MM-DDTHH:MM:SSZ`. MemoServ still stores timestamps internally as integer Unix epoch values.

## READ

Read a received memo:

```irc
/MEMOSERV READ 12
```

Only the recipient account can read a memo. Reading marks it read. If the memo does not exist, does not belong to the current account, or has been hidden from that account's inbox, MemoServ replies:

```text
No such memo.
```

A read reply includes the memo creation time in UTC text:

```text
Memo #12 from Alice at 2026-09-11T13:45:00Z: Please check the channel policy when you get back.
```

## REPLY

Reply to the sender of a received memo:

```irc
/MEMOSERV REPLY 12 :Thanks, I will look at it today.
```

The original memo must be visible in the current account's inbox. `REPLY` creates a new memo addressed to the original sender.

## FORWARD

Forward a received memo to another enabled NickServ account:

```irc
/MEMOSERV FORWARD 12 Bob
```

The original memo must be visible in the current account's inbox. The forwarded memo is stored as a new memo from the forwarding account. The original memo text is reused.

## DEL, DELETE, and DELSENT

Delete one received memo:

```irc
/MEMOSERV DEL 12
```

Delete all received memos:

```irc
/MEMOSERV DEL ALL
```

`DELETE` is accepted as an alias for `DEL`.

Hide one memo from your sent history:

```irc
/MEMOSERV DELSENT 13
```

Hide all memos from your sent history:

```irc
/MEMOSERV DELSENT ALL
```

`DEL` and `DELETE` remove recipient-side inbox visibility from `LIST`, `READ`, `REPLY`, `FORWARD`, `STATUS`, unread counts, and quota accounting. They do not remove the sender's copy from `SENT`. `DELSENT` removes sender-side visibility from `SENT`; it does not remove the recipient's copy from the inbox. The two sides are independent.

Rows hidden from one side remain in storage while the other side can still see the memo. Retention cleanup or account deletion can physically remove those rows. Future maintenance may also physically remove rows after both sender and recipient sides are hidden.

Existing MemoServ databases are migrated automatically with per-side visibility fields. Existing memos remain visible in `LIST` and `SENT` unless the recipient later uses `DEL`/`DELETE` or the sender later uses `DELSENT`.

## STATUS

Show mailbox status:

```irc
/MEMOSERV STATUS
```

The reply reports visible inbox count, configured quota, and unread count:

```text
Memos: 4/100 stored, 2 unread.
```

## HELP

Show the built-in command overview:

```irc
/MEMOSERV HELP
```

Show help for one command:

```irc
/MEMOSERV HELP SEND
/MEMOSERV HELP READ
/MEMOSERV HELP DELETE
/MEMOSERV HELP DELSENT
```

Unknown help topics produce a short syntax reminder instead of exposing internal state.

## Account lifecycle

MemoServ sends only to enabled NickServ accounts. Disabled accounts cannot receive new memos, and users cannot identify to disabled accounts to read existing memos.

When a network administrator disables an account with `NSSET <account> ENABLED 0`, existing MemoServ rows are preserved. If the account is later re-enabled, its owner can identify again and manage its visible memos normally.

When a network administrator deletes an account with `NSDROP <account>`, MemoServ removes all rows where that account is either sender or recipient, including sender-hidden and recipient-hidden rows. This prevents dropped account names from leaving orphaned sent or received memo history behind.

## Retention

When `memoserv_retention_days` is nonzero, MemoServ removes expired memo rows by creation time, including rows hidden from one side's view. Normal MemoServ activity may trigger retention cleanup, but the cleanup is throttled internally so ordinary commands do not run a global purge every time.

## Network-administrator commands

These commands require network-administrator mode `+N`:

```text
MSINFO <account>
MSPURGE <account|*>
```

`MSINFO` reports visible stored inbox count, visible unread count, configured quota, and retention policy for one account. It does not display memo contents.

Example reply:

```text
MEMOSERV account=Daniel stored=4 unread=2 quota=100 retention_days=90
```

`MSPURGE <account>` deletes expired memos for one account. `MSPURGE *` deletes expired memos globally. If `memoserv_retention_days` is `0`, `MSPURGE` reports that automatic retention is disabled.

## Privacy and service identity

MemoServ does not store or expose client `real_ip`, `real_host`, or `display_host` in memo records. Persistent identity is strictly NickServ account-to-account.

The reserved nickname `MemoServ` cannot be occupied by a normal IRC client. MemoServ does not become visible by joining channels, and private memo text is not exposed through channel or user visibility commands.

Administrative inspection of memo contents is intentionally not part of the current command set.
