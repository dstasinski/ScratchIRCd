# ScratchIRCd MemoServ Guide

MemoServ is a virtual server service for persistent account-to-account messages. It is not a `Client`, never joins channels, and never appears in NAMES, WHO, ISON, LIST, or LUSERS. Memo ownership is tied to authenticated NickServ account names rather than current nicknames.

## Database

MemoServ stores its data in:

```text
memoserv_db = data/memoserv.db
```

Each memo records a generated numeric id, sender account, recipient account, message text, creation time, and read time. Reading a memo marks it read; memos remain stored until the recipient deletes them.

## Authentication

All MemoServ commands require an authenticated NickServ account except HELP. The sender identity written into a memo is the authenticated account name. A user may therefore change nicknames without changing memo ownership.

When a user successfully identifies through direct `IDENTIFY`, `NICKSERV IDENTIFY`, or SASL, MemoServ reports the number of unread memos if that count is nonzero. Memo contents are never displayed automatically.

## Sending a memo

```text
MEMOSERV SEND <account> :<message>
PRIVMSG MemoServ :SEND <account> :<message>
```

The destination must be an existing enabled NickServ account. The message may contain up to `IRCD_MEMOSERV_TEXT_MAX` characters (currently 400). The memo is stored even when the recipient is offline.

If the recipient is currently connected and identified, MemoServ sends a short notice containing the new memo id, but the memo remains persistent until deleted.

## Listing memos

```text
MEMOSERV LIST
```

MemoServ lists up to `IRCD_MEMOSERV_LIST_LIMIT` newest memos (currently 50), newest first. Each line contains the memo id, READ/UNREAD status, sender account, and creation timestamp.

## Reading a memo

```text
MEMOSERV READ <memo-id>
```

Only the recipient account may read the memo. Reading it sets its read timestamp the first time it is read.

## Status

```text
MEMOSERV STATUS
```

Reports the current unread memo count for the authenticated account.

## Deleting memos

```text
MEMOSERV DEL <memo-id>
MEMOSERV DEL ALL
```

`DELETE` is accepted as an alias for `DEL`. A user can delete only memos addressed to their authenticated account.

## Help

```text
MEMOSERV HELP
```

The initial 0.21 command set is:

```text
SEND <account> :<message>
LIST
READ <memo-id>
DEL <memo-id|ALL>
STATUS
HELP
```

## Privacy and service identity

MemoServ does not use or expose client `real_ip`, `real_host`, or `display_host` in memo records. Persistent identity is strictly NickServ account-to-account. The reserved nickname `MemoServ` cannot be occupied by a normal IRC client.
