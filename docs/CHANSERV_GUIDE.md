# ScratchIRCd ChanServ Guide

ChanServ is a virtual server service. It is not a `Client`, never joins a channel, and never appears in NAMES, WHO, ISON, or LUSERS. Channel authority is exercised directly by the server.

## Registering a channel

You must be identified to a NickServ account and be an owner or operator in the live channel:

```text
CHANSERV REGISTER #channel :optional description
```

The authenticated NickServ account becomes the founder. Registration sets service-controlled channel mode `+r` and makes the channel persistent. Empty registered channels are retained by server authority; after a daemon restart the registration is restored from `data/chanserv.db` when the channel is next referenced/joined.

The founder is automatically granted owner (`+q`) and operator (`+o`) privileges when joining while authenticated to the founder account.

## Information and deletion

```text
CHANSERV INFO #channel
CHANSERV DROP #channel
CHANSERV HELP
```

`INFO` is public. `DROP` requires authentication to the founder account. Dropping registration removes service-controlled `+r`; an empty channel can then disappear normally.

The traditional virtual-service form is also supported:

```text
PRIVMSG ChanServ :REGISTER #channel :description
PRIVMSG ChanServ :INFO #channel
PRIVMSG ChanServ :DROP #channel
```

## Network administrator commands

```text
CSINFO #channel
CSSET #channel DESCRIPTION <text>
CSSET #channel FOUNDER <NickServ-account>
CSSET #channel ENABLED <0|1>
CSDROP #channel
```

Founder changes require an existing enabled NickServ account. Disabling a registration removes persistent `+r` behavior until re-enabled.

## Persistence and ISUPPORT

Registered channels are stored in `data/chanserv.db`. Numeric 005 advertises enabled persistent channels using ScratchIRCd's project-specific token:

```text
PCHANNELS=#one,#two
```

This first ChanServ milestone intentionally does not yet persist arbitrary channel modes, topics, ban lists, or access lists. Those build on this registration/founder foundation in later milestones.
