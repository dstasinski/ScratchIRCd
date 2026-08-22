# ScratchIRCd KNOCK Guide

ScratchIRCd implements `KNOCK` as a request for channel staff to invite a user. KNOCK does not itself create an invitation and never bypasses channel access policy.

## Syntax

```text
KNOCK <channel> [:reason]
```

Example:

```text
KNOCK #help :Please invite me
```

If the request is accepted for delivery, channel staff at HALFOP (`+h`) or higher receive:

```text
:<nick>!<user>@<display_host> KNOCK <channel> :<reason>
```

The requester's public `display_host` is used. `real_ip` and `real_host` are never disclosed by KNOCK.

The requester receives a server NOTICE confirming that the request was delivered. Channel staff must still explicitly use:

```text
INVITE <nick> <channel>
```

before the requester gains invite state.

## Channel mode +K

```text
MODE <channel> +K
```

disables KNOCK for the channel. Attempts return the existing `ERR_CANNOTKNOCK` numeric 480 defined in `include/numerics.h`.

Removing the mode:

```text
MODE <channel> -K
```

allows KNOCK again when the channel otherwise requires help entering.

## When KNOCK is accepted

KNOCK is useful when the requester cannot simply JOIN because one of these access conditions is active:

- invite-only mode `+i`;
- a channel key `+k`;
- an active/full user limit `+l`.

A configured `+l` does not make the channel knockable until that limit is actually reached.

KNOCK is rejected when:

- the channel does not exist;
- the requester is already on the channel;
- channel mode `+K` is set;
- the requester currently matches the channel ban policy;
- the channel is open and can simply be joined;
- no HALFOP-or-higher channel staff are online to receive the request.

Failures use numeric 480 (`ERR_CANNOTKNOCK`) where appropriate.

## Authority and privacy

KNOCK notifications are sent only to HALFOP (`+h`), OP (`+o`), PROTECTED (`+a`), and OWNER (`+q`) members. VOICE (`+v`) and ordinary members do not receive them.

KNOCK never creates an invite, changes channel membership, changes ChanServ access, or alters any persistent channel state.
