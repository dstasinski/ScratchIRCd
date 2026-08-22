# ScratchIRCd SILENCE, WATCH, and WHOWAS Guide

ScratchIRCd 0.23 implements three runtime client-presence facilities: `SILENCE`, `WATCH`, and `WHOWAS`. These are deliberately in-memory IRC session state rather than SQLite-backed service data.

## SILENCE

`SILENCE` blocks direct user-to-user `PRIVMSG` and `NOTICE` delivery from matching public identities.

```text
SILENCE
SILENCE +<mask>
SILENCE -<mask>
```

Multiple `+mask`/`-mask` operations may be supplied in one command.

A mask is matched against the sender's public identity:

```text
nick!user@display_host
```

`real_ip` and `real_host` are never used or exposed by SILENCE. This preserves ScratchIRCd's three-field identity model and means vhosts/cloaks affect SILENCE in the same way they affect other normal client-visible identity checks.

Example:

```text
SILENCE +Trouble!*@*
```

blocks direct PRIVMSG/NOTICE traffic from nickname `Trouble` regardless of its displayed user/host. Delivery failure is silent to the sender so SILENCE does not reveal another user's ignore state.

A client may store up to 64 SILENCE masks. Numeric `271` lists masks and `272` ends the list. Numeric `511` reports a full list.

## WATCH

WATCH provides server-side nickname presence notifications.

```text
WATCH +Nick1 +Nick2
WATCH -Nick1
WATCH
```

Adding an entry immediately reports whether that nickname is currently online (`604`) or offline (`605`). Once watched, ScratchIRCd sends:

```text
600  nickname logged online
601  nickname logged offline
```

Nick changes are represented as the old watched nickname going offline and the new watched nickname going online, if both are on the watching client's list.

WATCH nick matching follows the server's advertised RFC1459 casemapping. Each client may watch up to 128 nicknames; numeric `512` reports a full list. Numeric `606` lists the configured watch entries and `607` terminates the listing.

WATCH state lasts only for the current connection. It is not associated with a NickServ account and is not persisted across reconnects or daemon restarts.

## WHOWAS

WHOWAS queries recently departed or renamed nickname identities:

```text
WHOWAS <nick>
WHOWAS <nick> <count>
```

ScratchIRCd keeps the newest 256 historical nickname records in a fixed-size server-local ring. A record is created when a registered client changes nickname or disconnects, including abrupt socket loss, KILL, clean QUIT, and server-driven disconnects.

WHOWAS returns the historical public identity through numeric `314` and the server/time information through numeric `312`, followed by `369`. If no matching nickname exists, numeric `406` is returned before `369`.

The WHOWAS record contains:

```text
nick
user
display_host
realname
server name
departure/nick-change time
```

It deliberately does **not** retain or disclose `real_ip` or `real_host`. Operators who need current real connection identity continue to use the existing operator WHOIS/USERIP mechanisms.

WHOWAS state is runtime-only and is cleared when the daemon process restarts.

## ISUPPORT

Numeric `005` advertises:

```text
WATCH=128 SILENCE=64
```

alongside the existing ScratchIRCd ISUPPORT tokens.
