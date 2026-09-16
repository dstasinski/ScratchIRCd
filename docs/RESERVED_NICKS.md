# Reserved Nickname Policy

ScratchIRCd has two distinct reserved-nickname classes:

1. **Service-only nicknames** owned by the server itself.
2. **Configured reserved nicknames** loaded from `ircd.conf`.

These classes are deliberately separate.

## Service-only nicknames

The built-in virtual service names are server-only forever:

```text
NickServ
ChanServ
MemoServ
```

No connected client may use or register these names. This restriction applies to ordinary users, IRC operators, and network administrators.

Matching is case-insensitive, so all of these are rejected as the same service-only nickname:

```text
NickServ
nickserv
NICKSERV
NiCkSeRv
```

A rejected attempt must use `ERR_RESERVEDNICK` from `include/numerics.h`; do not hard-code the numeric literal at call sites.

## Configured reserved nicknames

Additional reserved nicknames may be defined in `ircd.conf`:

```text
reserved_nicks = Admin,Root,OperServ
```

Configured reserved nicknames are intended for operator or administrative use.

Rules:

- Ordinary users may not use configured reserved nicknames.
- Ordinary users may not register configured reserved nicknames.
- IRC operators and network administrators may use configured reserved nicknames.
- IRC operators and network administrators may register configured reserved nicknames only when the normal NickServ registration rules otherwise allow it.

Unauthorized attempts must use `ERR_RESERVEDNICK` from `include/numerics.h`.

## Case handling

Reserved nickname matching is case-insensitive everywhere.

The configured spelling is preserved in memory for display, documentation, and operator readability, but all comparisons use case-insensitive matching.

Example:

```text
reserved_nicks = Admin,Root,OperServ
```

These all match the configured reserved nickname `Admin`:

```text
Admin
admin
ADMIN
aDmIn
```

The config loader silently collapses case-only duplicates and preserves the first spelling seen:

```text
reserved_nicks = Admin,admin,ADMIN,Root,ROOT
```

loads as:

```text
Admin
Root
```

Empty comma-separated entries are ignored:

```text
reserved_nicks = Admin,,Root
```

loads as:

```text
Admin
Root
```

Invalid nickname syntax or nicknames longer than `IRC_NICK_MAX` remain configuration errors.

## Future casemapping note

The current implementation follows the project's existing nickname practice and uses `strcasecmp()` for reserved-nick comparisons. If ScratchIRCd later adopts a central RFC1459 casemapping helper, all reserved-nick matching should move to that shared canonical nickname comparison function.
