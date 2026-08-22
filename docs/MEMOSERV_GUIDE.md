# ScratchIRCd MemoServ Guide

MemoServ is a virtual server service for persistent account-to-account messages. It is not a `Client`, never joins channels, and never appears in NAMES, WHO, ISON, LIST, or LUSERS. Memo ownership is tied to authenticated NickServ account names rather than current nicknames.

## Database and policy

MemoServ stores data in:

```text
memoserv_db = data/memoserv.db
memoserv_quota = 100
memoserv_retention_days = 90
```

Quota is the maximum number of stored inbox memos for one recipient account. Retention is measured from memo creation time; `memoserv_retention_days = 0` disables automatic expiration. Expired memos are purged during normal MemoServ use and may also be purged explicitly by a network administrator.

Each memo records a generated numeric id, sender account, recipient account, message text, creation time, and read time. Reading marks it read; it remains stored until deletion or retention expiry.

## Authentication

All MemoServ commands except HELP require an authenticated NickServ account. The sender identity stored with a memo is the authenticated account name, so nickname changes do not alter memo ownership.

After successful direct IDENTIFY, NickServ IDENTIFY, or SASL, MemoServ reports the unread count when nonzero. Memo contents are never displayed automatically.

## Commands

```text
MEMOSERV SEND <account> :<message>
MEMOSERV LIST
MEMOSERV SENT
MEMOSERV READ <memo-id>
MEMOSERV REPLY <memo-id> :<message>
MEMOSERV FORWARD <memo-id> <account>
MEMOSERV DEL <memo-id>
MEMOSERV DEL ALL
MEMOSERV STATUS
MEMOSERV HELP
```

The traditional `PRIVMSG MemoServ :<command>` form is also supported.

`SEND` requires an existing enabled NickServ destination and enforces the destination mailbox quota. If the recipient is online and identified, a short new-memo notice is delivered immediately while the memo remains persistent.

`LIST` shows the newest received memos with READ/UNREAD state. `SENT` shows the newest memos whose sender is the authenticated account; this is derived from the same database row and does not create a duplicate sent-message copy.

`READ` is restricted to the recipient account and marks the memo read. `REPLY` sends a new memo back to the original sender of a recipient-owned memo. `FORWARD` sends the original memo text to another enabled account and records the forwarding account as the new sender.

`STATUS` reports stored inbox count, configured quota, and unread count. `DEL`/`DELETE` removes recipient-owned memos only.

## Network-administrator commands

```text
MSINFO <account>
MSPURGE <account|*>
```

`MSINFO` reports stored count, unread count, server quota, and retention policy without displaying memo contents. `MSPURGE` deletes expired memos using the configured retention period, either for one account or globally with `*`. These commands require network-administrator mode `+N`.

## Privacy

MemoServ does not store or expose client `real_ip`, `real_host`, or `display_host` in memo records. Persistent identity is strictly NickServ account-to-account. The reserved nickname `MemoServ` cannot be occupied by a normal IRC client.
