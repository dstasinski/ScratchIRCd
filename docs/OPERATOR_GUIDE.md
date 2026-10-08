# ScratchIRCd IRC Operator Guide

This guide covers commands and privileged operations available to authenticated IRC operators. Permissions are assigned individually; operator status alone does not grant every permission. Network-administrator-only commands belong in the [Network Administrator Guide](NETWORK_ADMIN_GUIDE.md). General client and service commands belong in their respective guides.

## IRC operator commands

### Authentication and operator modes

Authenticate with the operator name and password assigned to you:

```text
/OPER helper operator-password
```

Successful authentication grants `+o` and the permissions assigned to that operator. Operator records cannot be granted network-administrator mode `+N`.

Operators can control these operator-specific modes on themselves:

```text
/MODE helper +gHsIW
/MODE helper -H
```

- `g` receives and permits sending `GLOBOPS` and `LOCOPS`.
- `H` hides operator status from ordinary users.
- `I` hides idle time from ordinary users.
- `s` receives selected server notices; use `/SNOTICE` to choose categories.
- `W` reports when an ordinary user performs `WHOIS` on the operator.

### Operator command reference

#### DEAF

Any authenticated IRC operator or network administrator may set or clear `+D` on a client. A `+D` client cannot exchange direct `PRIVMSG` or `NOTICE` traffic with ordinary users; operators and network administrators are exempt.

```text
/DEAF trouble
/DEAF +trouble
/DEAF -trouble
```

A bare nickname enables `+D`; disabling requires the `-` form.

#### DIE

Requires `can_die`. Requests a graceful server shutdown.

```text
/DIE
```

#### FLASH

Any authenticated IRC operator or network administrator may send a server-originated `RPL_FLASH` numeric (`343`) to a channel, a comma-separated nickname list, or all registered clients.

```text
/FLASH #chat :This is a message to the channel
/FLASH alice,bob :This is a message to selected clients
/FLASH * :This is a message to all connected clients
```

#### GEOBAN

Requires `can_geoban`. Adds or lists country, region, ASN, or organization policies. Durations accept `s` (seconds), `m` (minutes), `h` (hours), `d` (days), and `w` (weeks). Timed policies expire automatically. Omit the duration to use `geoban_default_duration_seconds` (3600 seconds by default). `0`, `permanent`, `perm`, and `forever` mean permanent.

```text
/GEOBAN COUNTRY RU :Uses the configured default expiration
/GEOBAN COUNTRY RU 0 :Connections from this country are not accepted
/GEOBAN REGION AZ 30m :Temporary regional restriction (expires after 30 minutes)
/GEOBAN ASN AS22773 2h :Network abuse (expires after 2 hours)
/GEOBAN COUNTRY CN 7d :Temporary country restriction (expires after 7 days)
/GEOBAN ORG {*Example Network*} 2w :Temporary provider restriction (expires after 2 weeks)
/GEOBAN ORG {*Example Network*} forever :Blocked provider family
/GEOBAN LIST
```

#### GLOBOPS

Requires operator status and user mode `+g`. Sends a message to local operators with `+g`.

```text
/GLOBOPS :Scheduled maintenance begins in ten minutes
```

#### KILL

Requires `can_kill`. Disconnects a client.

```text
/KILL trouble :Abusive behavior
```

#### KLINE

Adding a KLINE requires `can_kline`; removing one requires `can_unkline`. Syntax: `/KLINE <nickname|user@host> [duration] [:reason]`. When omitted, the duration comes from `kline_default_duration_seconds` (3600 seconds by default). The duration accepts seconds, `m`, `h`, `d`, or `w`; `0` or `permanent` makes the ban non-expiring. Nickname targets are resolved to their real hostname or IP; explicit masks match real identity, not displayed cloaks.

```text
/KLINE trouble
/KLINE trouble 30m :Flooding
/KLINE *@bad.example 7d :Repeated abuse
/KLINE *@bad.example 0 :Permanent ban
/KLINE -*@bad.example
```

The default duration applies to both nickname and explicit-mask entries. Use `/KLINE -mask` to remove an entry early.

#### LOCOPS

Requires operator status and user mode `+g`. Sends a local operator message.

```text
/LOCOPS :Please review #help
```

#### MUTE

Any authenticated IRC operator or network administrator may set or clear `+M`. It blocks channel messages from an ordinary member; channel privileges and IRC operator status provide immunity.

```text
/MUTE trouble
/MUTE +trouble
/MUTE -trouble
```

A bare nickname enables `+M`; disabling requires the `-` form.

#### OPER

Authenticates a configured operator or the bootstrap network administrator.

```text
/OPER helper operator-password
```

#### REHASH

Requires `can_rehash`. Reloads settings that can change safely while the server is running. Listener and TLS changes require `/RESTART`.

```text
/REHASH
```

#### RESTART

Requires `can_restart`. Gracefully tears down and rebuilds the server in the same process.

```text
/RESTART
```

#### SAJOIN

Requires `can_override`. Forces a client into one or more channels.

```text
/SAJOIN alice #help
/SAJOIN alice #help,#staff
```

#### SAMODE

Requires `can_override`. Applies user or channel modes with server authority. User SAMODE cannot manufacture security or provenance modes such as `+N`, `+o`, `+r`, `+S`, `+t`, `+V`, `+x`, or `+z`.

```text
/SAMODE alice +i
/SAMODE #chat +o alice
/SAMODE #chat -b *!*@bad.example
```

#### SAPART

Requires `can_override`. Forces a client out of one or more channels.

```text
/SAPART alice #chat
/SAPART alice #chat,#help
```

#### SETHOST

Requires `can_override`. Changes only the client's displayed hostname.

```text
/SETHOST alice users/alice
```

#### SETIDENT

Requires `can_override`. Changes the client's displayed username.

```text
/SETIDENT alice newident
```

#### SETNAME

Requires `can_override`. Changes the client's real-name field.

```text
/SETNAME alice :Alice Example
```

#### SNOTICE

Any authenticated IRC operator or network administrator may query or change its server-notice category mask. `*` means every category.

```text
/SNOTICE
/SNOTICE +*
/SNOTICE -*
/SNOTICE +ks
/SNOTICE -f
```

Category letters are `c` connections, `o` operator activity, `k` kills, `b` KLINE/ZLINE activity, `g` GeoBAN activity, `w` WebIRC, `d` DNS, `s` security, `a` administration, `v` services, `r` registrations, `x` identity changes, `m` moderation, and `f` flood/resource events.

#### UNGEOBAN

Requires `can_geoban`. Removes a GeoBAN policy.

```text
/UNGEOBAN COUNTRY RU
/UNGEOBAN ASN AS22773
/UNGEOBAN ORG {*Example Network*}
```

#### USERIP

Any authenticated IRC operator or network administrator may inspect the real IP addresses of online clients.

```text
/USERIP alice
/USERIP alice bob carol
```

#### WALLOPS

Requires `can_wallops`. Sends a wallops message to clients with user mode `+w`.

```text
/WALLOPS :Network maintenance begins shortly
```

#### ZLINE

Requires `can_zline`. Syntax: `/ZLINE <nickname|IP-mask> [duration] [:reason]`. When omitted, the duration comes from `zline_default_duration_seconds` (3600 seconds by default). A duration of `0` or `permanent` creates a non-expiring ban. Nickname shorthand resolves to the client's real IP; explicit IP masks and CIDR ranges are also supported.

```text
/ZLINE trouble
/ZLINE trouble 2h :Repeated flooding
/ZLINE 203.0.113.* 7d :Abusive network
/ZLINE 203.0.113.0/24 0 :Permanent restriction
/ZLINE -203.0.113.*
```

### Privileged command forms

The following forms belong in this privileged section even though the command names also have ordinary uses.

An IRC operator or network administrator can control persistent channel logging:

```text
/CHANSERV SET #chat LOGGING ON
/CHANSERV SET #chat LOGGING OFF
```

Ban and GeoBAN statistics selectors require operator status:

```text
/STATS k
/STATS z
/STATS g
```

An operator's `/WHOIS` reply includes real identity information not disclosed to ordinary users:

```text
/WHOIS alice
```

