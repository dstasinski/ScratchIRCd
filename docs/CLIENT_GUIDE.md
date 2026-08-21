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

## IRCv3 CAP, SASL, and history

ScratchIRCd currently advertises:

```text
account-notify batch draft/chathistory sasl server-time
```

SASL mechanism `PLAIN` authenticates against the same `data/nickserv.db` account database used by NickServ IDENTIFY.

A typical SASL negotiation is:

```text
CAP LS 302
CAP REQ :sasl
NICK <nickname>
USER <username> 0 * :<real name>
AUTHENTICATE PLAIN
AUTHENTICATE <base64 PLAIN payload>
CAP END
```

While CAP negotiation is open, ScratchIRCd deliberately holds normal IRC registration until `CAP END`. Successful SASL returns numeric `903`, attaches the NickServ account before registration completes, sets `+r`, and applies any NickServ vhost exactly as IDENTIFY would. Failed SASL returns numeric `904`; the client may still send `CAP END` and register without an authenticated account.

The PLAIN payload represents `authzid NUL authcid NUL password`. ScratchIRCd permits an empty authorization identity or one equal to the authentication account. This first implementation accepts one base64 AUTHENTICATE data frame of at most 400 characters.

For full persistent-history presentation, a client may negotiate:

```text
CAP REQ :batch draft/chathistory server-time
```

Only `draft/chathistory` is required to use the current history command. `batch` packages the response as a `chathistory` batch, while `server-time` adds original UTC timestamps. After joining a channel, request:

```text
CHATHISTORY LATEST <channel> * <limit>
```

ScratchIRCd currently stores accepted channel PRIVMSG and NOTICE traffic only. See `docs/IRCV3_GUIDE.md` for the detailed history scope and limitations.

## Hostname privacy

Ordinary IRC clients see only another user's **displayed hostname**. WHO, WHOIS, USERHOST, JOIN/PART/QUIT, messages, channel activity, and channel ban masks use this displayed value.

A user's actual IP and verified DNS hostname are server-security information and are not exposed to ordinary clients. A vhost (`+t`) replaces only the displayed hostname. Planned cloak mode `+x` will work the same way. Channel `+b`, `+e`, and `+I` masks therefore match displayed `nick!user@host` identity rather than hidden real identity.

Historical channel records also store the displayed identity that was public when the message was sent; replay does not reveal `real_ip` or `real_host`.

`USERIP` exists but is restricted to IRC operators.

## NickServ accounts

NickServ is a virtual service. It can receive commands, but it is not a connected IRC client, never joins channels, and does not appear in NAMES, WHO, ISON, or LUSERS. The names `NickServ`, `ChanServ`, and `MemoServ` are reserved.

ScratchIRCd accepts both the direct form:

```text
NICKSERV <command> [parameters]
```

and the traditional form:

```text
PRIVMSG NickServ :<command> [parameters]
```

### Register and identify

```text
NICKSERV REGISTER <password>
NICKSERV IDENTIFY <password>
NICKSERV IDENTIFY <account> <password>
IDENTIFY <password>
IDENTIFY <account> <password>
```

Successful identification sets service-controlled user mode `+r`. An account vhost changes only the displayed hostname and sets `+t`.

### Password and email settings

```text
NICKSERV SET PASSWORD <new-password>
NICKSERV SET EMAIL <address>
NICKSERV VERIFY <token>
```

`SET PASSWORD` and `SET EMAIL` require identification. Email addresses are not trusted until the verification token sent by the server is confirmed with `VERIFY`.

### Recover a registered nickname

```text
NICKSERV RECOVER <nick>
```

You must be identified to the account matching `<nick>`. Default RECOVER safely renames the occupying client to a generated `Guest<connection-id>` nickname. It does not disconnect them. You can then use the normal:

```text
NICK <nick>
```

command to take the freed nickname.

To disconnect the occupying connection instead:

```text
NICKSERV RECOVER <nick> KILL
```

`GHOST` is a KILL alias:

```text
NICKSERV GHOST <nick>
```

### Email password reset

Request a reset:

```text
NICKSERV RESET <account>
```

ScratchIRCd always gives the same generic response whether or not the account exists. If the account is enabled and has a verified email address, a one-time reset token is emailed.

Complete the reset:

```text
NICKSERV RESET <account> <token> <new-password>
```

Reset tokens expire and can be used only once. The resulting password is stored only as an Argon2id hash.

See `docs/NICKSERV_GUIDE.md` for the complete NickServ guide.

## Currently implemented client commands

### ADMIN

```text
ADMIN
```

Displays configured server administration/location/contact information.

### AUTHENTICATE

```text
AUTHENTICATE PLAIN
AUTHENTICATE <base64-data>
AUTHENTICATE *
```

Used during IRCv3 SASL negotiation after requesting the `sasl` capability. `*` aborts the current SASL exchange.

### AWAY

```text
AWAY :<message>
AWAY
```

Sets or clears away status. Users sending a direct PRIVMSG to an away client receive the away message.

### CAP

```text
CAP LS 302
CAP LIST
CAP REQ :<capability> [capability...]
CAP END
```

Negotiates IRCv3 capabilities. ScratchIRCd currently advertises `account-notify`, `batch`, `draft/chathistory`, `sasl`, and `server-time`. Capability removals use a leading `-` in CAP REQ. Capability names are case-sensitive.

### CHATHISTORY

```text
CHATHISTORY LATEST <channel> * <limit>
```

Returns the most recent persisted PRIVMSG/NOTICE records for a channel. The client must have negotiated `draft/chathistory` and must currently be in the requested channel. `batch` is optional and encloses playback in a `chathistory` batch; `server-time` is optional and adds original timestamps.

### IDENTIFY

```text
IDENTIFY <password>
IDENTIFY <account> <password>
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

### NICKSERV

```text
NICKSERV <service-command> [parameters]
```

Direct alias for the virtual NickServ service. Implemented service commands are `REGISTER`, `IDENTIFY`, `RECOVER`, `GHOST`, `SET PASSWORD`, `SET EMAIL`, `VERIFY`, `RESET`, and `HELP`.

### NOTICE

```text
NOTICE <nickname> :<text>
NOTICE <channel> :<text>
```

Sends a notice. NOTICE failures are normally silent. Channel mode `+T` blocks channel notices. Accepted channel NOTICEs are stored in persistent history. Recipients that negotiated `server-time` receive a UTC time tag on live NOTICE delivery.

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

Sends private/channel messages or addresses the virtual NickServ service. Delivery observes user/channel modes such as moderated and registered-user restrictions. Accepted channel PRIVMSGs are stored in persistent history. Recipients that negotiated `server-time` receive a UTC time tag on live PRIVMSG delivery.

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

Displays public information about a user, including displayed hostname, visible channel membership, authenticated account state, away state, and idle/signon information where permitted. Real IP/DNS identity is not returned to ordinary users.

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
