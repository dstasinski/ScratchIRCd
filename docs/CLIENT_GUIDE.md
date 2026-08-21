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

ScratchIRCd supports IPv4 and IPv6 connections. FCrDNS, GeoIP and optional DNSBL policy are handled before registration completes without blocking the IRC event loop.

## Hostname privacy

Ordinary IRC clients see only another user's **displayed hostname**. WHO, WHOIS, USERHOST, JOIN/PART/QUIT, messages, channel activity, and channel ban masks use this displayed value.

A user's actual IP and verified DNS hostname are server-security information and are not exposed to ordinary clients. A vhost (`+t`) replaces only the displayed hostname. Planned cloak mode `+x` will work the same way. Channel `+b`, `+e`, and `+I` masks therefore match displayed `nick!user@host` identity rather than hidden real identity.

`USERIP` exists but is restricted to IRC operators.

## NickServ accounts

NickServ is a virtual service. It can receive messages, but it is not a normal connected client, never joins channels, and does not appear in NAMES, WHO, ISON, or LUSERS. The names `NickServ`, `ChanServ`, and `MemoServ` are reserved so users cannot impersonate services.

Register your current nickname:

```text
PRIVMSG NickServ :REGISTER <password>
```

Identify to the account matching your current nickname:

```text
IDENTIFY <password>
```

or:

```text
PRIVMSG NickServ :IDENTIFY <password>
```

Identify to a different registered account name while keeping your current IRC nickname:

```text
IDENTIFY <nick> <password>
PRIVMSG NickServ :IDENTIFY <nick> <password>
```

Successful identification sets user mode `+r` and stores the authenticated account separately from the current IRC nickname. An account with a configured vhost also changes only the displayed hostname and sets `+t`.

Change the password of the account to which you are currently identified:

```text
PRIVMSG NickServ :SET PASSWORD <new-password>
```

Show the currently implemented service help:

```text
PRIVMSG NickServ :HELP
```

NickServ passwords are stored only as Argon2id hashes. Account switching within one connection is currently intentionally disallowed; reconnect to identify to a different account.

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

### IDENTIFY

```text
IDENTIFY <password>
IDENTIFY <nick> <password>
```

Authenticates to a NickServ account and sets service-controlled user mode `+r`. A configured NickServ vhost is applied to the displayed hostname.

### INVITE

```text
INVITE <nickname> <channel>
```

Invites a user to a channel. Channel mode `+V` disables invitations. Explicit invitations can satisfy `+i` invite-only access but do not bypass unrelated restrictions such as bans, keys, limits, TLS requirements, or account requirements.

### ISON

```text
ISON <nick1> <nick2> ...
```

Returns requested nicknames that are currently online. Virtual services are intentionally not ordinary online-client entries.

### JOIN

```text
JOIN <channel>
JOIN <channel> <key>
```

Joins a channel. Channel names may begin with `#` or `&`. `&` channels are private/unlisted. JOIN enforces keys, limits, bans/exceptions, invite restrictions, throttling, redirects, account/oper/admin requirements, and TLS-only restrictions.

### KICK

```text
KICK <channel> <nickname> :<reason>
```

Removes a member when the requester has sufficient channel privilege. Privilege hierarchy is owner (`+q`) > operator (`+o`) > halfop (`+h`) > voice (`+v`) > normal member.

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

Queries or changes user/channel modes subject to authority rules. Security/service-derived modes such as `+r`, `+o`, `+N`, `+t`, `+V`, and `+z` cannot be self-granted.

### MOTD

```text
MOTD
```

Displays the server message of the day.

### NAMES

```text
NAMES <channel>
```

Displays visible channel members. Membership prefixes include `~` owner, `@` operator, `%` halfop, and `+` voice.

### NICK

```text
NICK <new-nickname>
```

Sets or changes your nickname. Internal service names are reserved.

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

Supplies an optional server connection password before registration.

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
PRIVMSG NickServ :<service-command>
```

Sends private/channel messages or addresses the virtual NickServ service. Delivery observes user/channel modes such as moderated and registered-user restrictions.

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

Displays public information about a user, including displayed hostname, visible channel membership, registered state, away state, and idle/signon information where permitted. Real IP/DNS identity is not returned to ordinary users.

## User modes

ScratchIRCd defines these client modes:

- `B` — bot marker.
- `d` — suppress ordinary channel PRIVMSGs except configured command-prefix traffic; full behavior still planned.
- `g` — globops/locops capability; full behavior still planned.
- `H` — hide IRCop status; IRCop-only behavior still planned.
- `h` — HelpOp.
- `I` — hide operator idle time from regular users.
- `i` — invisible in general WHO results.
- `N` — network administrator.
- `o` — IRC operator.
- `p` — hide channel membership from WHOIS.
- `R` — accept PRIVMSG/NOTICE only from authenticated (`+r`) users.
- `r` — authenticated to a registered NickServ account; service-controlled and implemented.
- `S` — services daemon protection marker.
- `s` — server notices; full behavior still planned.
- `T` — reject CTCPs.
- `t` — using a vhost; changes only displayed hostname.
- `V` — authenticated WebIRC client marker.
- `W` — WHOIS notification for IRCops; full behavior still planned.
- `w` — receive WALLOPS messages.
- `x` — use a cloaked displayed hostname; cloak generation still planned.
- `z` — secure/TLS client marker.

## Channel modes

- `A` — network administrators only.
- `B <channel>` — redirect banned clients.
- `b <mask>` — ban displayed `nick!user@host` identity.
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
- `M` — authenticated NickServ account (`+r`) required to speak.
- `m` — moderated; voice/halfop/op/owner may speak.
- `n` — no outside channel messages.
- `O` — IRC operators only.
- `o <nick>` — channel operator.
- `p` — private channel.
- `q <nick>` — channel owner.
- `r` — registered channel marker; ChanServ-controlled behavior is planned.
- `R` — authenticated NickServ account (`+r`) required to join.
- `S` — strip incoming colors; filtering behavior planned.
- `s` — secret channel.
- `t` — halfop or higher required to set topic.
- `T` — channel NOTICEs prohibited.
- `V` — INVITE prohibited.
- `v <nick>` — voice.
- `z` — TLS clients only.

## Planned client commands

The remaining general client commands from the project specification not yet implemented include:

```text
SILENCE
WATCH [+|-]<nick> ...
WHOWAS
```

ChanServ and MemoServ commands will be added here as those virtual SQLite-backed services are implemented.
