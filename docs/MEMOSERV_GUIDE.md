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

`memoserv_db` is the SQLite database path. `memoserv_quota` is the maximum number of stored inbox memos for one recipient account. `memoserv_retention_days` controls automatic expiration by memo creation time. A value of `0` disables automatic expiration.

Each memo stores a generated numeric ID, sender account, recipient account, message text, creation time, and read time. Reading a memo marks it read, but it remains stored until deletion or retention expiry.

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
#12 UNREAD from Alice at 1789012345
```

Show sent memos:

```irc
/MEMOSERV SENT
```

A sent memo row currently looks like this:

```text
#13 TO Bob READ at 1789012400
```

`LIST` and `SENT` show at most the configured internal list limit for one reply set. Current timestamps are Unix epoch values. Human-readable timestamp formatting is planned for a later MemoServ polish pass.

## READ

Read a received memo:

```irc
/MEMOSERV READ 12
```

Only the recipient account can read a memo. Reading marks it read. If the memo does not exist or does not belong to the current account, MemoServ replies:

```text
No such memo.
```

## REPLY

Reply to the sender of a received memo:

```irc
/MEMOSERV REPLY 12 :Thanks, I will look at it today.
```

The original memo must be owned by the current account. `REPLY` creates a new memo addressed to the original sender.

## FORWARD

Forward a received memo to another enabled NickServ account:

```irc
/MEMOSERV FORWARD 12 Bob
```

The forwarded memo is stored as a new memo from the forwarding account. The original memo text is reused.

## DEL and DELETE

Delete one received memo:

```irc
/MEMOSERV DEL 12
```

Delete all received memos:

```irc
/MEMOSERV DEL ALL
```

`DELETE` is accepted as an alias for `DEL`.

Deletion affects recipient-owned inbox memos. `SENT` can show sent history, but there is not currently a separate command for deleting only sent-history entries.

## STATUS

Show mailbox status:

```irc
/MEMOSERV STATUS
```

The reply reports stored inbox count, configured quota, and unread count:

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
```

Unknown help topics produce a short syntax reminder instead of exposing internal state.

## Account lifecycle

MemoServ sends only to enabled NickServ accounts. Disabled accounts cannot receive new memos, and users cannot identify to disabled accounts to read existing memos.

When a network administrator disables an account with `NSSET <account> ENABLED 0`, existing MemoServ rows are preserved. If the account is later re-enabled, its owner can identify again and manage its stored memos normally.

When a network administrator deletes an account with `NSDROP <account>`, MemoServ removes all rows where that account is either sender or recipient. This prevents dropped account names from leaving orphaned sent or received memo history behind.

## Retention

When `memoserv_retention_days` is nonzero, MemoServ can remove expired memos by creation time. Normal MemoServ activity may trigger retention cleanup, but the cleanup is throttled internally so ordinary commands do not run a global purge every time.

## Network-administrator commands

These commands require network-administrator mode `+N`:

```text
MSINFO <account>
MSPURGE <account|*>
```

`MSINFO` reports stored count, unread count, configured quota, and retention policy for one account. It does not display memo contents.

Example reply:

```text
MEMOSERV account=Daniel stored=4 unread=2 quota=100 retention_days=90
```

`MSPURGE <account>` deletes expired memos for one account. `MSPURGE *` deletes expired memos globally. If `memoserv_retention_days` is `0`, `MSPURGE` reports that automatic retention is disabled.

## Privacy and service identity

MemoServ does not store or expose client `real_ip`, `real_host`, or `display_host` in memo records. Persistent identity is strictly NickServ account-to-account.

The reserved nickname `MemoServ` cannot be occupied by a normal IRC client. MemoServ does not become visible by joining channels, and private memo text is not exposed through channel or user visibility commands.

Administrative inspection of memo contents is intentionally not part of the current command set.
