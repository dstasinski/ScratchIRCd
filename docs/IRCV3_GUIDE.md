# ScratchIRCd IRCv3 Guide

ScratchIRCd uses a general capability bitset and capability registry rather than treating individual IRCv3 extensions as one-off client flags.

## CAP negotiation

The server currently advertises:

```text
account-notify batch draft/chathistory sasl server-time
```

Clients may negotiate capabilities before registration:

```text
CAP LS 302
CAP REQ :account-notify batch draft/chathistory sasl server-time
NICK example
USER example 0 * :Example User
CAP END
```

Registration remains held while CAP negotiation is active. `CAP END` releases the registration hold once the ordinary registration prerequisites are satisfied.

`CAP LIST` returns the capabilities currently enabled for that connection.

`CAP REQ` accepts multiple space-separated capability names and supports removal with a leading `-`:

```text
CAP REQ :account-notify sasl
CAP REQ :-sasl
```

Requests are atomic. If any requested capability is unknown, the server sends `NAK` and does not apply any part of that request.

## sasl

The `sasl` capability enables the existing `AUTHENTICATE PLAIN` flow. SASL uses the NickServ account database and therefore produces the same authenticated account, `+r`, and account vhost state as NickServ IDENTIFY.

See `docs/NICKSERV_GUIDE.md` for account management details.

## account-notify

A client that enables `account-notify` receives IRCv3 `ACCOUNT` messages when another registered client sharing a channel becomes authenticated after registration:

```text
:nick!user@display.host ACCOUNT accountname
```

Only clients that negotiated `account-notify` receive these messages. A recipient sharing more than one channel with the account-changing user receives one notification rather than one notification per shared channel.

Account state established by SASL before registration does not generate an `ACCOUNT` notification because the client has not yet become visible to channel peers.

The notification path covers all current post-registration NickServ authentication entry points: direct `IDENTIFY`, direct `NICKSERV ...`, and `PRIVMSG NickServ :...`.

## Persistent channel history

Accepted channel `PRIVMSG` and `NOTICE` events are persisted to:

```text
data/history.db
```

The path is configurable with:

```text
history_db = data/history.db
history_limit = 100
```

`history_limit` limits the number of records returned by a single history request. The compiled hard ceiling is `IRCD_HISTORY_HARD_LIMIT`, currently 500.

Only messages that pass normal live channel policy are stored. Messages rejected by `+n`, `+m`, `+M`, or other delivery checks never enter history.

History records retain the public identity that was visible when the message was sent:

```text
nick
user
display_host
account_name
command
channel
text
timestamp
```

Real IP addresses and real DNS hostnames are intentionally not stored in the history record. Replaying history therefore cannot reveal identity information that ordinary clients were not allowed to see originally.

### CHATHISTORY LATEST

ScratchIRCd currently implements the conservative first subset of the IRCv3 work-in-progress CHATHISTORY command:

```text
CHATHISTORY LATEST <channel> * <limit>
```

The requesting client must:

1. Be registered.
2. Have negotiated both `draft/chathistory` and `batch`.
3. Currently be a member of the requested channel.

The request limit is capped by the configured `history_limit` and the compiled hard maximum.

Example negotiation and request:

```text
CAP REQ :batch draft/chathistory server-time
CAP END
JOIN #example
CHATHISTORY LATEST #example * 50
```

History is returned in a `chathistory` batch:

```text
:irc.example BATCH +h123 chathistory #example
@batch=h123;time=2026-08-20T20:00:00.123Z :alice!alice@example PRIVMSG #example :hello
@batch=h123;time=2026-08-20T20:01:00.456Z :bob!bob@example NOTICE #example :notice text
:irc.example BATCH -h123
```

If `server-time` is enabled, every replayed record includes its original UTC timestamp in a `time` tag. If `server-time` is not enabled, the batch tag is still supplied but the time tag is omitted.

The SQLite database persists across daemon restarts. A channel may therefore be recreated later and, once the requester has joined it, its previously stored history can be requested.

### Current history scope

The 0.17 implementation intentionally stores and retrieves channel PRIVMSG/NOTICE history only. Direct-message history, BEFORE/AFTER/BETWEEN/AROUND subcommands, message IDs, automatic playback on JOIN, event playback, and history retention/expiry policies are reserved for later milestones.

ScratchIRCd advertises:

```text
MSGREFTYPES=timestamp
```

in ISUPPORT to reflect that this first implementation uses timestamps rather than message IDs as its history reference foundation.

## batch

The `batch` capability is currently used for CHATHISTORY playback. Clients requesting history must negotiate it.

## server-time

The `server-time` capability causes replayed historical messages to include their original timestamps. Live-message server-time tagging will be expanded as the IRCv3 message-tag layer develops.

## Development direction

New IRCv3 capabilities should be represented by capability bits, advertised through the CAP registry, and used to gate capability-specific protocol output. This avoids independent boolean fields and one-off negotiation logic.

The next history work can build on this foundation with broader CHATHISTORY reference modes, message IDs, retention controls, private-message history policy, and eventual persistent-channel integration through ChanServ.
