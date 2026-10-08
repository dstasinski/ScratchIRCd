# ScratchIRCd Client Guide

This guide covers connecting an ordinary IRC client and general IRC commands available to regular users. Service-specific commands are documented separately in the NickServ, ChanServ, and MemoServ guides; operator, network-administrator, and gateway-only commands are omitted.

## Connecting

Connect to the hostname and port supplied by the network administrator. ScratchIRCd accepts IPv4 and IPv6 connections and may provide both a plaintext listener (commonly port 6667) and a TLS listener (commonly port 6697). Prefer TLS when it is available.

If the server requires a connection password, send `/PASS` before registration. Then send `/NICK` and `/USER`:

```text
/PASS server-password
/NICK alice
/USER alice 0 * :Alice Example
```

Nicknames are at most 15 characters, usernames are at most 10 characters, and channel names are at most 32 characters.

ScratchIRCd automatically enables user mode `+x` when registration completes. Other clients see only the resulting displayed hostname; ordinary `WHO`, `WHOIS`, `USERHOST`, messages, and channel activity do not reveal the connection's real IP address or verified hostname.

### IRCv3 capability negotiation

ScratchIRCd advertises these capabilities through CAP 302:

```text
account-notify away-notify batch draft/chathistory extended-join labeled-response message-tags sasl=PLAIN server-time
```

A basic negotiation is:

```text
/CAP LS 302
/CAP REQ :server-time message-tags
/CAP END
```

Registration waits while CAP negotiation is open, so always finish with `/CAP END`.

### No-spoof and client-version checks

When no-spoof protection is enabled, the server sends a random PING cookie during registration. Return that cookie exactly:

```text
/PING :cookie-from-server
/PONG :cookie-from-server
```

After the cookie is verified, the server requests CTCP VERSION. Most IRC clients answer automatically; a raw client answers the server name with a NOTICE:

```text
:irc.example.net PRIVMSG alice :\001VERSION\001
/NOTICE irc.example.net :\001VERSION ExampleClient 1.0\001
```

Until the VERSION reply is received, the client cannot join channels or send `/PRIVMSG`/`/NOTICE` traffic except to an IRC operator or network administrator. An authenticated WebIRC connection may also receive a CTCP WEBSITE request and should answer it in the same way.

### SASL PLAIN

SASL authenticates a NickServ account during connection. Request `sasl`, not `sasl=PLAIN`:

```text
/CAP LS 302
/CAP REQ :sasl
/NICK alice
/USER alice 0 * :Alice Example
/AUTHENTICATE PLAIN
/AUTHENTICATE <base64-frame-1>
/AUTHENTICATE <base64-final-frame>
/CAP END
```

The decoded PLAIN payload is `authzid NUL authcid NUL password`. `authzid` may be empty or equal to the account name. Send the base64 text in frames of no more than 400 characters. A frame shorter than 400 characters is the final frame. If the total is an exact multiple of 400, send `AUTHENTICATE +` afterward. ScratchIRCd accepts at most 800 encoded characters. `AUTHENTICATE *` aborts the exchange. Successful authentication returns numeric `903`, attaches the account, and enables service-controlled mode `+r`; invalid credentials return `904`, an oversized payload returns `905`, and cancellation returns `906`. A failed exchange does not prevent unauthenticated registration after `/CAP END`.

### Persistent channel history

Negotiate history before registration:

```text
/CAP REQ :batch draft/chathistory server-time
```

After joining a channel, request its latest messages:

```text
/CHATHISTORY LATEST #chat * 50
```

`draft/chathistory` is required. `batch` groups the reply, and `server-time` adds original UTC timestamps. The requester must currently be in the channel.

## Client command reference

### ADMIN

Displays the configured server administration contact and location.

```text
/ADMIN
```

### AUTHENTICATE

Continues or aborts an IRCv3 SASL exchange after the `sasl` capability is requested.

```text
/AUTHENTICATE PLAIN
/AUTHENTICATE <base64-frame>
/AUTHENTICATE +
/AUTHENTICATE *
```

### AWAY

Sets or clears away status.

```text
/AWAY :Out to lunch
/AWAY
```

### CAP

Lists, requests, removes, and finishes IRCv3 capability negotiation. A leading `-` removes a capability.

```text
/CAP LS 302
/CAP LIST
/CAP REQ :server-time message-tags
/CAP REQ :-message-tags
/CAP END
```

### CHATHISTORY

Returns recent channel `PRIVMSG` and `NOTICE` history. The requester must have negotiated `draft/chathistory` and be in the channel.

```text
/CHATHISTORY LATEST #chat * 50
```

### INFO

Displays information about the running ScratchIRCd software.

```text
/INFO
```

### INVITE

Invites a nickname to a channel. An invitation can satisfy mode `+i`, but it does not bypass bans, keys, limits, TLS requirements, or account requirements. Channel mode `+V` disables invitations.

```text
/INVITE bob #chat
```

### ISON

Returns the requested nicknames that are currently online.

```text
/ISON alice bob carol
```

### JOIN

Joins a `#` or `&` channel, optionally using its key. `&` channels are private and unlisted.

```text
/JOIN #chat
/JOIN #private secret-key
```

### KICK

Removes a member when the requester has sufficient channel authority.

```text
/KICK #chat bob :Flooding
```

Channel authority is owner (`+q`), protected (`+a`), operator (`+o`), halfop (`+h`), voice (`+v`), then ordinary member. Protected members cannot be kicked by an ordinary channel operator or halfop.

### KNOCK

Asks halfop-or-higher channel staff for entry to a restricted channel. It does not create an invitation. Banned users cannot knock, and channel mode `+K` disables it.

```text
/KNOCK #private :May I join?
```

### LINKS

Shows this server in the network link list. ScratchIRCd is a single-server daemon; an optional mask is accepted.

```text
/LINKS
/LINKS *.example.net
```

### LIST

Lists channels visible to the requester. Private, secret, and unlisted channels are hidden as applicable.

```text
/LIST
```

### LUSERS

Displays current user and channel counts.

```text
/LUSERS
```

### MODE

Queries or changes user and channel modes.

```text
/MODE alice
/MODE alice +iR
/MODE #chat
/MODE #chat +nt
/MODE #chat +o bob
/MODE #chat +b *!*@bad.example
/MODE #chat b
```

An ordinary client can set or clear its own modes: `B` bot, `d` suppress ordinary channel messages except configured command-prefix traffic, `i` invisible, `p` hide channel membership from `WHOIS`, `R` accept messages only from identified users, `T` reject CTCP, `w` receive wallops, and `x` use the configured cloak. Mode `+x` is applied automatically at registration and may be cleared or reapplied. Modes `g` and `s` require operator status. Other security, transport, service, and authentication modes cannot be self-granted.

Channel boolean modes are:

- `A` network-administrator-only joins; `O` IRC-operator-only joins.
- `c` reject color/control codes; `S` strip them.
- `i` invite-only; `K` disable `KNOCK`; `V` disable `INVITE`.
- `M` identified users only may speak; `R` identified users only may join.
- `m` moderated; `n` no outside messages.
- `p` private; `s` secret; `t` topic changes require halfop or higher.
- `T` disallow channel notices; `z` require TLS.
- `r` is the service-controlled registered-channel marker.

Membership modes are `q <nick>` owner, `a <nick>` protected, `o <nick>` operator, `h <nick>` halfop, and `v <nick>` voice. List modes are `b <mask>` ban, `e <mask>` ban exception, and `I <mask>` invite exception; query a list by omitting the mask. Parameter modes are `j <joins:seconds>` join throttle, `k <key>` key, `l <count>` user limit, `L <channel>` full-channel redirect, and `B <channel>` banned-client redirect.

### MOTD

Displays the message of the day.

```text
/MOTD
```

### NAMES

Displays visible channel members. Prefixes are `~` owner, `&` protected, `@` operator, `%` halfop, and `+` voice.

```text
/NAMES
/NAMES #chat
```

### NICK

Sets or changes the nickname.

```text
/NICK alice
/NICK alice_away
```

### NOTICE

Sends a notice to a nickname or channel. Errors are normally silent. Channel mode `+T` blocks channel notices.

```text
/NOTICE bob :Meeting starts now
/NOTICE #chat :Meeting starts now
```

### PART

Leaves a channel, optionally with a reason.

```text
/PART #chat
/PART #chat :Good night
```

### PASS

Supplies a server connection password before registration.

```text
/PASS server-password
```

### PING

Asks the server to return a matching `PONG`.

```text
/PING client-token
```

### PONG

Answers a server `PING`. Return the token exactly; unrelated commands do not clear an outstanding liveness challenge.

```text
/PONG server-token
```

### PRIVMSG

Sends a private or channel message, or addresses a virtual service.

```text
/PRIVMSG bob :Hello
/PRIVMSG #chat :Hello everyone
/PRIVMSG NickServ :IDENTIFY account-password
```

A client's reported `/WHOIS` idle time resets only after one of its private or channel `PRIVMSG` messages is successfully delivered. No other activity, including `NOTICE`, `TAGMSG`, `PING`, or `WHOIS`, resets that messaging-idle timer.

### QUIT

Disconnects from the server.

```text
/QUIT :Leaving
```

### RULES

Displays the server's rules.

```text
/RULES
```

### SILENCE

Lists, adds, or removes masks on the client's personal silence list. Matching direct messages are suppressed.

```text
/SILENCE
/SILENCE +*!*@noisy.example
/SILENCE -*!*@noisy.example
```

### STATS

Lists available statistics selectors or displays server uptime.

```text
/STATS
/STATS ?
/STATS h
/STATS u
```

### TAGMSG

Relays client-only IRCv3 tags without a message body. Sender and recipient must have negotiated `message-tags`; normal message-access rules still apply.

```text
/@+typing=active TAGMSG bob
/@+react=thumbsup TAGMSG #chat
```

### TIME

Displays the server's local date and time.

```text
/TIME
```

### TOPIC

Queries or changes a channel topic. With channel mode `+t`, halfop or higher is required to change it.

```text
/TOPIC #chat
/TOPIC #chat :Welcome to the channel
```

### USER

Supplies the username and real name during initial registration.

```text
/USER alice 0 * :Alice Example
```

### USERHOST

Returns displayed hostnames for online nicknames.

```text
/USERHOST alice bob carol
```

### VERSION

Displays the server software version.

```text
/VERSION
```

### WATCH

Manages the bounded nickname watch list and reports watched users' presence changes.

```text
/WATCH
/WATCH +alice +bob
/WATCH -alice
/WATCH s
/WATCH l
/WATCH c
```

`WATCH` with no parameters lists every nickname in your watch list. `WATCH +nick` adds a nickname and immediately reports whether it is online. `WATCH -nick` removes that nickname from your list. `WATCH s` shows your watch-list status, including how many nicknames you watch and how many other clients are watching your current nickname. `WATCH l` lists only watched nicknames that are currently online. `WATCH c` clears your entire watch list.

### WHO

Displays visible users matching a channel or mask. `WHO 0` performs a general query while respecting invisibility and channel visibility.

```text
/WHO #chat
/WHO alice
/WHO 0
```

### WHOIS

Displays public information about a user, including displayed hostname, visible channels, account and away state, sign-on time, and permitted idle information.

```text
/WHOIS alice
```

### WHOWAS

Returns bounded recent historical identity information for a nickname that is no longer online. An optional count limits replies.

```text
/WHOWAS alice
/WHOWAS alice 5
```
