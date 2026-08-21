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

Registration remains held while CAP negotiation is active. `CAP END` releases the registration hold once the ordinary registration prerequisites are satisfied. `CAP LIST` returns the capabilities currently enabled for that connection.

`CAP REQ` accepts multiple space-separated capability names and supports removal with a leading `-`. Requests are atomic: if any requested capability is unknown, the server sends `NAK` and applies none of the request. Capability names are case-sensitive as required by IRCv3.

## sasl

The `sasl` capability enables `AUTHENTICATE PLAIN`. SASL uses the NickServ account database and therefore produces the same authenticated account, `+r`, and account vhost state as NickServ IDENTIFY.

## account-notify

A client that enables `account-notify` receives IRCv3 `ACCOUNT` messages when another registered client sharing a channel becomes authenticated after registration:

```text
:nick!user@display.host ACCOUNT accountname
```

A recipient sharing more than one channel with the changing user receives one notification rather than one per shared channel. Account state established by SASL before registration does not produce an ACCOUNT message because the user is not yet visible to peers.

## Persistent channel history

Accepted channel `PRIVMSG` and `NOTICE` events are persisted to:

```text
data/history.db
```

Configuration:

```text
history_db = data/history.db
history_limit = 100
```

`history_limit` is the maximum number of records returned by one request; the compiled hard ceiling is currently 500. Only traffic that passes normal channel policy is stored.

History records retain the public identity visible at send time: nickname, username, `display_host`, authenticated account, command, channel, text, and timestamp. `real_ip` and `real_host` are deliberately not stored in replayable history records.

### CHATHISTORY LATEST

ScratchIRCd 0.17 implements this first subset of the IRCv3 work-in-progress CHATHISTORY extension:

```text
CHATHISTORY LATEST <channel> * <limit>
```

The requester must be registered, must have negotiated `draft/chathistory`, and must currently be a member of the channel. `batch` and `server-time` are optional enhancements rather than additional prerequisites.

With `batch` enabled, playback is enclosed in the standardized `chathistory` batch type. With `server-time` enabled, each historical message carries its original UTC timestamp. Without `batch`, records are returned directly; without `server-time`, the `time` tag is omitted.

Example:

```text
CAP REQ :batch draft/chathistory server-time
CAP END
JOIN #example
CHATHISTORY LATEST #example * 50
```

Typical response:

```text
:irc.example BATCH +h123 chathistory #example
@batch=h123;time=2026-08-20T20:00:00.123Z :alice!alice@example PRIVMSG #example :hello
@batch=h123;time=2026-08-20T20:01:00.456Z :bob!bob@example NOTICE #example :notice text
:irc.example BATCH -h123
```

The database persists across daemon restarts. A channel may be recreated later and its earlier history requested after the user rejoins it.

ScratchIRCd advertises both:

```text
MSGREFTYPES=timestamp
CHATHISTORY=<configured-history-limit>
```

through numeric 005.

### Current history scope

The 0.17 implementation stores and retrieves channel PRIVMSG/NOTICE history only. Direct-message history, BEFORE/AFTER/BETWEEN/AROUND, message IDs, automatic JOIN playback, event playback, and retention/expiry policy remain future work.

## batch

`batch` is used to package CHATHISTORY playback when negotiated. It is not mandatory for the limited LATEST implementation.

## server-time

When negotiated, `server-time` adds UTC `time` tags to live PRIVMSG/NOTICE delivery and preserves original timestamps on replayed history. The capability is never used before the client's CAP request has been acknowledged.

## Development direction

New IRCv3 capabilities should be represented by capability bits, advertised through the CAP registry, and used to gate capability-specific output. Future history work can add broader reference modes, message IDs, retention controls, private-message policy, and persistent-channel integration through ChanServ.
