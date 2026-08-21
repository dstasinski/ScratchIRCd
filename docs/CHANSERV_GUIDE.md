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

INFO reports the registered channel name, founder account, description, and registration time.

## Dropping a registration

The authenticated founder may remove the registration:

```text
CHANSERV DROP #channel
PRIVMSG ChanServ :DROP #channel
```

The live channel loses service-controlled `+r`. The channel may continue to exist normally while clients remain in it.

## Persistence and founder privileges

Channel registrations persist in SQLite even when the in-memory channel becomes empty and is reclaimed. When the channel is later recreated by JOIN, ScratchIRCd restores registered mode `+r` from `chanserv.db`.

When the authenticated founder joins a registered channel, ChanServ authority automatically grants channel owner and operator privileges (`+q` and `+o`). The founder account may therefore use a different current nickname and still receive founder authority.

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

`CSSET ... FOUNDER` requires an existing enabled NickServ account. Disabling a channel removes the live service-controlled `+r` state; re-enabling it restores `+r` when the channel is next restored or referenced through ChanServ management.

## Current 0.18 scope

This foundation persists channel registration, founder account, description, enabled state, and timestamps. It does not yet persist arbitrary modes, topics, masks, access lists, or per-account automatic `+o/+h/+v` entries. Those features are intended to build on this schema in later milestones.
