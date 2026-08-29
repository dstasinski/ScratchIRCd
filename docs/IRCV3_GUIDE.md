# ScratchIRCd IRCv3 Guide

ScratchIRCd uses a general capability bitset and capability registry rather than treating individual IRCv3 extensions as one-off client flags.

## CAP negotiation

The server currently advertises:

```text
account-notify away-notify batch draft/chathistory extended-join labeled-response message-tags sasl=PLAIN server-time
```

Clients may negotiate capabilities before registration:

```text
CAP LS 302
CAP REQ :account-notify away-notify batch draft/chathistory extended-join labeled-response message-tags sasl server-time
NICK example
USER example 0 * :Example User
CAP END
```

Registration remains held while CAP negotiation is active. `CAP END` releases the registration hold once the ordinary registration prerequisites are satisfied. `CAP LIST` returns the capabilities currently enabled for that connection.

`CAP LS 302` includes the supported SASL mechanism as `sasl=PLAIN`; legacy `CAP LS` advertises the capability without a value. Clients request the capability name `sasl`, not its advertised value.

`CAP REQ` accepts multiple space-separated capability names and supports removal with a leading `-`. Requests are atomic: if any requested capability is unknown or would leave `labeled-response` enabled without its required `batch` dependency, the server sends `NAK` and applies none of the request. Capability names are case-sensitive as required by IRCv3. Newly enabled behavior starts after the ACK has been sent.

## sasl

The `sasl` capability enables `AUTHENTICATE PLAIN`. SASL uses the NickServ account database and therefore produces the same authenticated account, `+r`, and account vhost state as NickServ IDENTIFY.

## account-notify

A client that enables `account-notify` receives IRCv3 `ACCOUNT` messages when another registered client sharing a channel becomes authenticated after registration:

```text
:nick!user@display.host ACCOUNT accountname
```

A recipient sharing more than one channel with the changing user receives one notification rather than one per shared channel. Account state established by SASL before registration does not produce an ACCOUNT message because the user is not yet visible to peers.

## away-notify

Capable clients receive `AWAY` when a user sharing one or more channels sets, changes, or clears away state. Notifications are deduplicated across shared channels and are not reflected back to the changing client. If an already-away user joins a channel, capable peers receive that user's current away state immediately after JOIN.

## extended-join

JOIN is formatted per recipient. A client with `extended-join` receives the joining user's account name (or `*`) and real name:

```text
:nick!user@display.host JOIN #channel account :Real Name
```

Clients without the capability continue to receive the traditional JOIN form.

## message-tags and TAGMSG

ScratchIRCd parses IRCv3 client tag sections independently from the classic 510-byte message budget. Client-only `+` tags are relayed unchanged on `PRIVMSG`, `NOTICE`, and `TAGMSG` to recipients that negotiated `message-tags`; unrecognized unprefixed client tags are stripped. Client tag data is bounded to the IRCv3 4094-byte data limit, and overlong input receives numeric 417 without being truncated.

`TAGMSG <target>` follows ordinary direct/channel message access policy and is delivered only to `message-tags` recipients.

## labeled-response

`labeled-response` requires `batch`. A valid client `label` tag is copied onto a `labeled-response` batch that groups the command's replies. Commands with no ordinary response receive a labeled `ACK`. Nested response batches, including CHATHISTORY, retain valid batch nesting. Labels are limited to 64 decoded bytes.

## Persistent channel history

Accepted channel `PRIVMSG` and `NOTICE` events are persisted to:

```text
data/history.db
```

Configuration:

```text
history_db = data/history.db
history_limit = 100
history_retention_days = 30
history_max_rows = 250000
```

`history_limit` is the maximum number of records returned by one request; the compiled hard ceiling is currently 500. `history_retention_days` controls age-based expiration and may be set to `0` to disable age expiration. `history_max_rows` remains the global row ceiling even when age retention is disabled. Maintenance is throttled rather than performed on every message. Only traffic that passes normal channel policy is stored.

History records retain the public identity visible at send time: nickname, username, `display_host`, authenticated account, command, channel, text, and timestamp. `real_ip` and `real_host` are deliberately not stored in replayable history records.

### CHATHISTORY LATEST

ScratchIRCd 0.34 implements the following intentionally limited CHATHISTORY subset:

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

The current implementation stores and retrieves channel PRIVMSG/NOTICE history only. Direct-message history, BEFORE/AFTER/BETWEEN/AROUND, message IDs, automatic JOIN playback, and event playback are not implemented. Persistent history is bounded by the configured age-retention and global-row policies described above.

## batch

`batch` is used to package CHATHISTORY playback when negotiated. It is not mandatory for the limited LATEST implementation.

## server-time

When negotiated, `server-time` adds millisecond UTC `time` tags throughout server output, including numerics and presence events. Historical messages retain their original stored timestamps rather than replay time. The capability is never used before the enabling CAP request has been acknowledged, and an existing `time` tag is never overwritten.

## Development direction

New IRCv3 capabilities should be represented by capability bits, advertised through the CAP registry, and used to gate capability-specific output. Any future history expansion should preserve the existing bounded-storage and wire-safety rules while adding only protocol scope that is deliberately selected after 1.0 hardening.
