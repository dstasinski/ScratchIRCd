# ScratchIRCd MemoServ Guide

MemoServ is ScratchIRCd's persistent account-to-account memo service. It is a virtual server service, not a normal IRC client. It never joins channels and never appears in ordinary NAMES, WHO, ISON, LIST, or LUSERS output.

Memo ownership is tied to authenticated NickServ account names, not to temporary nicknames. A user must be connected, registered, and identified to a NickServ account before using MemoServ commands that read, send, reply, forward, delete, or report mailbox status.

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

If the sender has reached `memoserv_sender_quota`, MemoServ replies:

```text
You have reached your outstanding sent-memo limit of 100. Capacity returns when recipients delete memos or retention expires them.
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

`DEL` and `DELETE` remove recipient-side inbox visibility from `LIST`, `READ`, `REPLY`, `FORWARD`, `STATUS`, unread counts, quota accounting, and the sender's outstanding sent-memo quota. They do not remove the sender's copy from `SENT`. `DELSENT` removes sender-side visibility from `SENT`; it does not remove the recipient's copy from the inbox and does not by itself free sender quota. The two sides are independent.

Rows hidden from one side remain in storage while the other side can still see the memo. When both the sender and recipient sides are hidden, MemoServ physically removes the row during the delete operation that hides the second side. Retention cleanup or account deletion can also physically remove hidden rows.

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

Memos belong to identified NickServ accounts. Disabled accounts cannot receive new memos or sign in to read existing ones. Existing memos are retained while an account is disabled; deleting an account removes its associated sent and received memos. Account administration is documented in the Network Administrator Guide.

## Retention

Memos can expire under the network's configured retention policy. Expired memos are removed by creation time, including memos hidden from one side's view. Contact a network administrator for the current retention period.

## Privacy and service identity

MemoServ does not store or expose client `real_ip`, `real_host`, or `display_host` in memo records. Persistent identity is strictly NickServ account-to-account.

The reserved nickname `MemoServ` cannot be occupied by a normal IRC client. MemoServ does not become visible by joining channels, and private memo text is not exposed through channel or user visibility commands.

Administrative inspection of memo contents is intentionally not part of the current command set.
