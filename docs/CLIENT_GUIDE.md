# ScratchIRCd Client Guide

This guide documents commands and modes intended for ordinary IRC clients. It reflects the current implementation and will be expanded as ScratchIRCd develops.

## Connecting and registering

If the server requires a connection password, send PASS before registration:

```text
PASS <server-password>
```

Set nickname and user information with:

```text
NICK <nickname>
USER <username> 0 * :<real name>
```

ScratchIRCd supports IPv4 and IPv6 connections. DNS lookup is asynchronous and does not block other clients. Persistent KLINE/ZLINE policy is checked before registration completes.

## Hostname privacy

Ordinary IRC clients see only another user's **displayed hostname**. This is the hostname used by WHO, WHOIS, USERHOST, JOIN/PART/QUIT, messages, channel activity, and channel ban masks.

A user's actual IP address and verified DNS hostname are server-security information and are not exposed to ordinary clients. A vhost (`+t`) replaces the displayed hostname without changing the real identity. Planned cloak mode `+x` will similarly replace only the displayed hostname.

Because channel bans are normal channel-visible policy, `+b`, `+e`, and `+I` masks match the displayed `nick!user@host` identity, not hidden real IP/DNS data.

`USERIP` exists but is restricted to IRC operators.

## Currently implemented client commands

### ADMIN

```text
ADMIN
```

Displays configured server administration/location/contact information.

### AWAY

```text
AWAY :<message>
AWAY
```

Sets or clears away status. Users sending a direct PRIVMSG to an away client receive the away message.

### INVITE

```text
INVITE <nickname> <channel>
```

Invites a user to a channel. Channel mode `+V` disables invitations. Explicit invitations can satisfy `+i` invite-only access but do not bypass unrelated restrictions such as bans, keys, limits, TLS requirements, or account requirements.

### ISON

```text
ISON <nick1> <nick2> ...
```

Returns the requested nicknames that are currently online.

### JOIN

```text
JOIN <channel>
JOIN <channel> <key>
```

Joins a channel. Channel names may begin with `#` or `&`. `&` channels are private/unlisted. JOIN enforces applicable keys, limits, bans/exceptions, invite restrictions, throttling, redirects, registration/oper/admin requirements, and TLS-only restrictions.

### KICK

```text
KICK <channel> <nickname> :<reason>
```

Removes a member when the requester has sufficient channel privilege. Channel privilege hierarchy is owner (`+q`) > operator (`+o`) > halfop (`+h`) > voice (`+v`) > normal member.

### LIST

```text
LIST
```

Lists channels visible to the requester. Private/secret/unlisted channels are hidden as appropriate.

### LUSERS

```text
LUSERS
```

Displays server user/channel statistics.

### MODE

```text
MODE <nickname>
MODE <nickname> <modes>
MODE <channel>
MODE <channel> <modes> [parameters...]
```

Queries or changes user/channel modes subject to authority rules.

### MOTD

```text
MOTD
```

Displays the server message of the day.

### NAMES

```text
NAMES <channel>
```

Displays visible members of a channel. Membership prefixes include `~` owner, `@` operator, `%` halfop, and `+` voice.

### NICK

```text
NICK <new-nickname>
```

Sets or changes your nickname.

### NOTICE

```text
NOTICE <nickname> :<text>
NOTICE <channel> :<text>
```

Sends a notice. NOTICE failures are normally silent. Channel mode `+T` blocks channel notices.

### PART

```text
PART <channel>
PART <channel> :<reason>
```

Leaves a channel.

### PASS

```text
PASS <server-password>
```

Supplies an optional server connection password. When configured, successful PASS is required before registration completes.

### PING / PONG

```text
PING <token>
PONG <token>
```

Connection keepalive commands.

### PRIVMSG

```text
PRIVMSG <nickname> :<text>
PRIVMSG <channel> :<text>
```

Sends a private or channel message. Delivery observes user/channel modes such as moderated channels and registered-user restrictions.

### QUIT

```text
QUIT :<reason>
```

Disconnects from the server.

### RULES

```text
RULES
```

Displays the configured server rules file.

### TOPIC

```text
TOPIC <channel>
TOPIC <channel> :<new topic>
```

Queries or changes a channel topic. With channel mode `+t`, halfop or higher is required to change it.

### USERHOST

```text
USERHOST <nick1> [nick2 ...]
```

Returns the displayed hostname for online nicknames. It does not reveal real IP/DNS identity.

### WHO

```text
WHO <mask-or-channel>
WHO 0
```

Displays visible matching users while respecting invisibility and channel visibility rules. WHO uses only displayed hostnames.

### WHOIS

```text
WHOIS <nickname>
```

Displays public information about a user, including the displayed hostname, visible channel membership, away state, and idle/signon information where permitted. Real IP/DNS identity is not returned to ordinary users.

## User modes

ScratchIRCd defines these client modes:

- `B` — bot marker.
- `d` — suppress ordinary channel PRIVMSGs except configured command-prefix traffic; behavior still planned.
- `g` — globops/locops capability; behavior still planned.
- `H` — hide IRCop status; IRCop-only behavior still planned.
- `h` — HelpOp.
- `I` — hide operator idle time from regular users.
- `i` — invisible in general WHO results.
- `N` — network administrator.
- `o` — IRC operator.
- `p` — hide channel membership from WHOIS.
- `R` — accept PRIVMSG/NOTICE only from registered (`+r`) users.
- `r` — registered nickname marker; service-controlled behavior is planned.
- `S` — services daemon protection marker.
- `s` — server notices; behavior still planned.
- `T` — reject CTCPs.
- `t` — using a vhost; changes only the displayed hostname.
- `V` — WebIRC client marker.
- `W` — WHOIS notification for IRCops; behavior still planned.
- `w` — receive WALLOPS messages.
- `x` — use a cloaked displayed hostname; cloak generation is still planned.
- `z` — secure/TLS client marker.

Security/service-derived modes cannot simply be self-granted.

## Channel modes

ScratchIRCd defines the following channel modes. Some already have enforcement while some supporting behavior remains under development:

- `A` — network administrators only.
- `B <channel>` — redirect banned clients.
- `b <mask>` — ban the displayed `nick!user@host` identity.
- `c` — no ANSI color; full filtering behavior planned.
- `e <mask>` — channel-ban exception against displayed identity.
- `h <nick>` — halfop.
- `i` — invite only.
- `I <mask>` — invite exception against displayed identity.
- `j <joins:seconds>` — per-client join throttle.
- `K` — disallow KNOCK; KNOCK is not currently implemented.
- `k <key>` — channel key.
- `l <count>` — member limit.
- `L <channel>` — redirect when `+l` is full.
- `M` — registered nickname required to speak.
- `m` — moderated; voice/halfop/op/owner may speak.
- `n` — no outside channel messages.
- `O` — IRC operators only.
- `o <nick>` — channel operator.
- `p` — private channel.
- `q <nick>` — channel owner.
- `r` — registered channel marker; services-controlled.
- `R` — registered nickname required to join.
- `S` — strip incoming colors; filtering behavior planned.
- `s` — secret channel.
- `t` — halfop or higher required to set topic.
- `T` — channel NOTICEs prohibited.
- `V` — INVITE prohibited.
- `v <nick>` — voice.
- `z` — TLS clients only.

## Planned client commands

The project specification also calls for these general client commands, which are not implemented yet:

```text
IDENTIFY [nick] <password>
SILENCE
WATCH [+|-]<nick> ...
WHOWAS
```

Service commands for NickServ, ChanServ, and MemoServ will be documented here or in dedicated service guides as those SQLite-backed services are implemented.
