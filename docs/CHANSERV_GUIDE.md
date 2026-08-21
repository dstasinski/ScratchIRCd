# ScratchIRCd ChanServ Guide

ChanServ is a virtual server service backed by `data/chanserv.db`. It is not a `Client`, never joins channels, and never appears in NAMES, WHO, ISON, or LUSERS. Channel authority is tied to authenticated NickServ account names rather than the user's current nickname.

## Registering a channel

First identify to NickServ, join the channel, and have channel owner/operator privilege. Then use either form:

```text
CHANSERV REGISTER #channel :optional description
PRIVMSG ChanServ :REGISTER #channel :optional description
```

A successful registration records the authenticated NickServ account as founder and gives the live channel service-controlled mode `+r`.

## Channel information

```text
CHANSERV INFO #channel
PRIVMSG ChanServ :INFO #channel
```

INFO reports the registered channel name, founder account, description, stored mode-lock value, and registration time.

## Account access lists

The founder can grant persistent channel privileges to other enabled NickServ accounts:

```text
CHANSERV ACCESS #channel ADD <account> OWNER
CHANSERV ACCESS #channel ADD <account> OP
CHANSERV ACCESS #channel ADD <account> HALFOP
CHANSERV ACCESS #channel ADD <account> VOICE
CHANSERV ACCESS #channel DEL <account>
CHANSERV ACCESS #channel LIST
```

Access is bound to the authenticated account, not the current nickname. On JOIN, the corresponding privileges are restored automatically:

- `OWNER` -> channel owner/operator (`+q/+o`, `~` prefix)
- `OP` -> channel operator (`+o`, `@` prefix)
- `HALFOP` -> halfop (`+h`, `%` prefix)
- `VOICE` -> voice (`+v`, `+` prefix)

The founder is implicitly an owner and is not stored as a separate access-list entry.

## Persistent boolean modes

The founder can store a channel mode lock:

```text
CHANSERV SET #channel MLOCK +nt
```

The current 0.19 mode lock supports boolean channel modes only: `A c i K M m n O p R S s t T V z`. Service-controlled `+r` is always restored separately and cannot be placed in the lock. Parameter modes and lists such as `+k`, `+l`, `+j`, `+L`, `+B`, `+b`, `+e`, and `+I` are not persisted yet.

The stored mode-lock state is reapplied when a persistent channel is recreated/restored. Updating MLOCK also refreshes the current live registered channel.

## Persistent topic

The founder can store a persistent topic:

```text
CHANSERV SET #channel TOPIC :Persistent channel topic
```

ChanServ stores the topic text, setter identity, and timestamp in SQLite. When the channel is recreated after becoming empty or after a daemon restart, the topic is restored before JOIN completes so normal topic numerics show the saved value.

## Dropping a registration

The authenticated founder may remove the registration:

```text
CHANSERV DROP #channel
PRIVMSG ChanServ :DROP #channel
```

The live channel loses service-controlled `+r`. Its persistent access entries are deleted automatically with the registration. The live channel may continue to exist normally while clients remain in it.

## Persistence and founder privileges

Channel registrations, access lists, mode-lock state, and persistent topic data survive daemon restart in SQLite even when the in-memory channel becomes empty and is reclaimed. When the channel is later recreated by JOIN, ScratchIRCd restores `+r`, the boolean mode lock, topic state, and authenticated-account privileges.

## ISUPPORT PCHANNELS

Numeric 005 includes the ScratchIRCd-specific token:

```text
PCHANNELS=#one,#two,#three
```

The value lists enabled ChanServ registrations. When no persistent channels are registered the token is emitted as `PCHANNELS=`.

## Network-administrator commands

Network administrators may inspect and manage registrations directly:

```text
CSINFO #channel
CSSET #channel DESCRIPTION <text>
CSSET #channel FOUNDER <NickServ-account>
CSSET #channel ENABLED <0|1>
CSDROP #channel
```

`CSSET ... FOUNDER` requires an existing enabled NickServ account. Network administrators can also join/operate normally and use the founder-facing service commands when authenticated as the founder account. Direct administrator controls for individual access-list and mode-lock rows can be expanded later if needed.

## Current 0.19 scope

0.19 persists registration metadata, founder identity, account access roles, boolean mode-lock state, and topic state. Parameter modes, ban/exception/invex lists, keys, join throttles, limits/redirects, and automatic persistence of arbitrary live MODE/TOPIC changes remain future work.
