# ScratchIRCd IRC Operator Guide

This command-only guide is the complete reference for IRC operators and network administrators. Privileged commands and privileged forms appear first; every ordinary client command appears in the second section. Technical administration belongs in `NETWORK_ADMIN_GUIDE.md`.

## 1. IRC operator and network-administrator commands

### Authentication and operator modes

Authenticate with the operator name and password assigned to you:

```text
OPER helper operator-password
```

Successful authentication grants `+o` and the permissions assigned to that operator. The bootstrap network administrator also receives `+N` and every operator permission. Operator records cannot be granted `+N`.

Operators can control these operator-specific modes on themselves:

```text
MODE helper +gHsIW
MODE helper -H
```

- `g` receives and permits sending `GLOBOPS` and `LOCOPS`.
- `H` hides operator status from ordinary users.
- `I` hides idle time from ordinary users.
- `s` receives selected server notices; use `SNOTICE` to choose categories.
- `W` reports when an ordinary user performs `WHOIS` on the operator.

### Operator and administrator command reference

#### CSDROP

Network administrator only. Deletes a registered channel.

```text
CSDROP #chat
```

#### CSINFO

Network administrator only. Displays administrative information for a registered channel.

```text
CSINFO #chat
```

#### CSSET

Network administrator only. Changes a registered channel's description, founder, or enabled state.

```text
CSSET #chat DESCRIPTION :General discussion
CSSET #chat FOUNDER alice
CSSET #chat ENABLED 0
CSSET #chat ENABLED 1
```

#### DEAF

Any authenticated IRC operator or network administrator may set or clear `+D` on a client. A `+D` client cannot exchange direct `PRIVMSG` or `NOTICE` traffic with ordinary users; operators and network administrators are exempt.

```text
DEAF +trouble
DEAF -trouble
```

#### DIE

Requires `can_die`. Requests a graceful server shutdown.

```text
DIE
```

#### FLASH

Any authenticated IRC operator or network administrator may send a server-originated `RPL_FLASH` numeric (`343`) to a channel, a comma-separated nickname list, or all registered clients.

```text
FLASH #chat :This is a message to the channel
FLASH alice,bob :This is a message to selected clients
FLASH * :This is a message to all connected clients
```

#### GEOBAN

Requires `can_geoban`. Adds or lists country, region, ASN, or organization policies. Durations accept `s`, `m`, `h`, `d`, and `w`; `0`, `permanent`, `perm`, and `forever` mean permanent.

```text
GEOBAN COUNTRY RU 0 :Connections from this country are not accepted
GEOBAN REGION AZ 7d :Temporary regional restriction
GEOBAN ASN AS22773 1d :Network abuse
GEOBAN ORG {*Example Network*} forever :Blocked provider family
GEOBAN LIST
```

#### GLOBOPS

Requires operator status and user mode `+g`. Sends a message to local operators with `+g`.

```text
GLOBOPS :Scheduled maintenance begins in ten minutes
```

#### KILL

Requires `can_kill`. Disconnects a client.

```text
KILL trouble :Abusive behavior
```

#### KLINE

Adding a KLINE requires `can_kline`; removing one requires `can_unkline`. Nickname shorthand creates a temporary policy using the server defaults. Explicit masks are permanent and match `user@real_host` and `user@real_ip`, not a displayed cloak or vhost.

```text
KLINE trouble
KLINE *@bad.example :Abusive network
KLINE -*@bad.example
```

#### LOCOPS

Requires operator status and user mode `+g`. Sends a local operator message.

```text
LOCOPS :Please review #help
```

#### MSINFO

Network administrator only. Displays memo counts and policy information for an account.

```text
MSINFO alice
```

#### MSPURGE

Network administrator only. Purges expired memos for one account or all accounts according to the configured retention period.

```text
MSPURGE alice
MSPURGE *
```

#### MUTE

Any authenticated IRC operator or network administrator may set or clear `+M`. It blocks channel messages from an ordinary member; channel privileges and IRC operator status provide immunity.

```text
MUTE +trouble
MUTE -trouble
```

#### NSDROP

Network administrator only. Deletes a NickServ account.

```text
NSDROP alice
```

#### NSINFO

Network administrator only. Displays administrative account information.

```text
NSINFO alice
```

#### NSSET

Network administrator only. Changes account credentials, vhost, email, or enabled state. `-` clears a vhost or email address.

```text
NSSET alice PASSWORD new-password
NSSET alice VHOST users/alice
NSSET alice VHOST -
NSSET alice EMAIL alice@example.net
NSSET alice EMAIL -
NSSET alice ENABLED 0
NSSET alice ENABLED 1
```

#### OPER

Authenticates a configured operator or the bootstrap network administrator.

```text
OPER helper operator-password
```

#### OPERADD

Network administrator only. Creates an enabled operator. Use `-` for no vhost or no permissions; permission names are comma-separated.

```text
OPERADD helper strong-password staff.example :can_kill,can_kline,helpop,get_host
OPERADD announcer strong-password - :-
```

#### OPERDEL

Network administrator only. Deletes an operator.

```text
OPERDEL helper
```

#### OPERLIST

Network administrator only. Lists all operators or one named operator.

```text
OPERLIST
OPERLIST helper
```

#### OPERSET

Network administrator only. Changes an operator's name, password, permissions, vhost, or enabled state.

```text
OPERSET helper NAME newhelper
OPERSET helper PASSWORD new-password
OPERSET helper PERMISSIONS :can_kill,can_kline
OPERSET helper VHOST staff.example
OPERSET helper VHOST -
OPERSET helper ENABLED 0
OPERSET helper ENABLED 1
```

Valid permissions are `can_rehash`, `can_die`, `can_restart`, `helpop`, `can_wallops`, `can_kill`, `can_kline`, `can_unkline`, `can_zline`, `can_geoban`, `get_host`, and `can_override`.

#### REHASH

Requires `can_rehash`. Reloads settings that can change safely while the server is running. Listener and TLS changes require `RESTART`.

```text
REHASH
```

#### RESTART

Requires `can_restart`. Gracefully tears down and rebuilds the server in the same process.

```text
RESTART
```

#### SAJOIN

Requires `can_override`. Forces a client into one or more channels.

```text
SAJOIN alice #help
SAJOIN alice #help,#staff
```

#### SAMODE

Requires `can_override`. Applies user or channel modes with server authority. User SAMODE cannot manufacture security or provenance modes such as `+N`, `+o`, `+r`, `+S`, `+t`, `+V`, `+x`, or `+z`.

```text
SAMODE alice +i
SAMODE #chat +o alice
SAMODE #chat -b *!*@bad.example
```

#### SAPART

Requires `can_override`. Forces a client out of one or more channels.

```text
SAPART alice #chat
SAPART alice #chat,#help
```

#### SETHOST

Requires `can_override`. Changes only the client's displayed hostname.

```text
SETHOST alice users/alice
```

#### SETIDENT

Requires `can_override`. Changes the client's displayed username.

```text
SETIDENT alice newident
```

#### SETNAME

Requires `can_override`. Changes the client's real-name field.

```text
SETNAME alice :Alice Example
```

#### SNOTICE

Any authenticated IRC operator or network administrator may query or change its server-notice category mask. `*` means every category.

```text
SNOTICE
SNOTICE +*
SNOTICE -*
SNOTICE +ks
SNOTICE -f
```

Category letters are `c` connections, `o` operator activity, `k` kills, `b` KLINE/ZLINE activity, `g` GeoBAN activity, `w` WebIRC, `d` DNS, `s` security, `a` administration, `v` services, `r` registrations, `x` identity changes, `m` moderation, and `f` flood/resource events.

#### UNGEOBAN

Requires `can_geoban`. Removes a GeoBAN policy.

```text
UNGEOBAN COUNTRY RU
UNGEOBAN ASN AS22773
UNGEOBAN ORG {*Example Network*}
```

#### USERIP

Any authenticated IRC operator or network administrator may inspect the real IP addresses of online clients.

```text
USERIP alice
USERIP alice bob carol
```

#### WALLOPS

Requires `can_wallops`. Sends a wallops message to clients with user mode `+w`.

```text
WALLOPS :Network maintenance begins shortly
```

#### ZLINE

Requires `can_zline`. Nickname shorthand creates a temporary exact-IP policy using the server defaults. Explicit masks are permanent and match only the real client IP.

```text
ZLINE trouble
ZLINE 203.0.113.* :Abusive network
ZLINE -203.0.113.*
```

### Privileged forms of general commands

The following forms belong in this privileged section even though the command names also have ordinary uses.

Network administrators create or delete persistent channel registrations through either direct ChanServ syntax or the traditional service-message form. Registration also requires an identified NickServ account and owner/operator authority in the live channel.

```text
CHANSERV REGISTER #chat :General discussion
CHANSERV DROP #chat
PRIVMSG ChanServ :REGISTER #chat General discussion
PRIVMSG ChanServ :DROP #chat
```

An IRC operator or network administrator can control persistent channel logging:

```text
CHANSERV SET #chat LOGGING ON
CHANSERV SET #chat LOGGING OFF
```

Ban and GeoBAN statistics selectors require operator status:

```text
STATS k
STATS z
STATS g
```

An operator's `WHOIS` reply includes real identity information not disclosed to ordinary users:

```text
WHOIS alice
```

## 2. General client commands

This section contains the complete ordinary client command set. Operator status does not bypass ordinary channel authority, capability negotiation, or service-account checks unless a privileged command explicitly says otherwise.

### General command index and examples

| Command | Purpose | Example |
| --- | --- | --- |
| `ADMIN` | Show the server's administrator contact and location. | `ADMIN` |
| `AUTHENTICATE` | Continue, finish, or abort SASL PLAIN negotiation using frames of at most 400 characters. | `AUTHENTICATE PLAIN`<br>`AUTHENTICATE <base64-frame>`<br>`AUTHENTICATE +`<br>`AUTHENTICATE *` |
| `AWAY` | Set or clear away status. | `AWAY :Out to lunch`<br>`AWAY` |
| `CAP` | Negotiate IRCv3 capabilities. | `CAP LS 302`<br>`CAP REQ :server-time message-tags`<br>`CAP END` |
| `CHANSERV` | Use ordinary ChanServ functions. | `CHANSERV INFO #chat`<br>`CHANSERV ACCESS #chat LIST`<br>`CHANSERV HELP` |
| `CHATHISTORY` | Replay recent channel history. | `CHATHISTORY LATEST #chat * 50` |
| `IDENTIFY` | Authenticate a NickServ account. | `IDENTIFY password`<br>`IDENTIFY alice password` |
| `INFO` | Show server software information. | `INFO` |
| `INVITE` | Invite a user to a channel. | `INVITE bob #chat` |
| `ISON` | Test whether nicknames are online. | `ISON alice bob carol` |
| `JOIN` | Join a channel, optionally with a key. | `JOIN #chat`<br>`JOIN #private secret-key` |
| `KICK` | Remove a channel member with sufficient channel authority. | `KICK #chat bob :Flooding` |
| `KNOCK` | Ask channel staff for entry to a restricted channel. | `KNOCK #private :May I join?` |
| `LINKS` | Show the single server in the link list. | `LINKS`<br>`LINKS *.example.net` |
| `LIST` | List channels visible to the requester. | `LIST` |
| `LUSERS` | Show current user and channel counts. | `LUSERS` |
| `MEMOSERV` | Use account-to-account memo services. | `MEMOSERV LIST`<br>`MEMOSERV SEND bob :Please contact me` |
| `MODE` | Query or change user and channel modes. | `MODE alice +i`<br>`MODE #chat +nt`<br>`MODE #chat +o bob` |
| `MOTD` | Show the message of the day. | `MOTD` |
| `NAMES` | List visible members of all visible channels or one channel. | `NAMES`<br>`NAMES #chat` |
| `NICK` | Set or change a nickname. | `NICK alice` |
| `NICKSERV` | Register, identify, log out, or manage a personal account. | `NICKSERV IDENTIFY password`<br>`NICKSERV LOGOUT`<br>`NICKSERV HELP` |
| `NOTICE` | Send a notice to a user or channel. | `NOTICE bob :Meeting starts now`<br>`NOTICE #chat :Meeting starts now` |
| `PART` | Leave a channel. | `PART #chat :Good night` |
| `PASS` | Supply the server password before registration. | `PASS server-password` |
| `PING` | Request a matching PONG. | `PING client-token` |
| `PONG` | Answer a server PING with the exact token. | `PONG server-token` |
| `PRIVMSG` | Send a message to a user, channel, or virtual service. | `PRIVMSG bob :Hello`<br>`PRIVMSG #chat :Hello everyone` |
| `QUIT` | Disconnect from the server. | `QUIT :Leaving` |
| `RULES` | Show the server rules. | `RULES` |
| `SILENCE` | List, add, or remove personal silence masks. | `SILENCE`<br>`SILENCE +*!*@noisy.example`<br>`SILENCE -*!*@noisy.example` |
| `STATS` | List selectors or show uptime. | `STATS`<br>`STATS ?`<br>`STATS u` |
| `TAGMSG` | Relay client-only IRCv3 tags. | `@+typing=active TAGMSG bob`<br>`@+react=thumbsup TAGMSG #chat` |
| `TIME` | Show the server's local time. | `TIME` |
| `TOPIC` | Query or change a channel topic. | `TOPIC #chat`<br>`TOPIC #chat :Welcome` |
| `USER` | Supply username and real name during registration. | `USER alice 0 * :Alice Example` |
| `USERHOST` | Show displayed hostnames for online nicknames. | `USERHOST alice bob` |
| `VERSION` | Show the server version. | `VERSION` |
| `WATCH` | Query or change a nickname watch list. | `WATCH`<br>`WATCH +alice +bob -carol` |
| `WEBIRC` | Supply end-user identity from an authorized gateway before registration. | `WEBIRC password gateway.example client.example 203.0.113.25` |
| `WHO` | Show visible users matching a channel or nickname, or perform a general query. | `WHO #chat`<br>`WHO alice`<br>`WHO 0` |
| `WHOIS` | Show information about an online user. | `WHOIS alice` |
| `WHOWAS` | Show recent information for a nickname no longer online. | `WHOWAS alice`<br>`WHOWAS alice 5` |

### NickServ commands

NickServ supports direct commands and `PRIVMSG NickServ :<command>`. Successful identification sets service-controlled mode `+r`; `LOGOUT` detaches the account and removes account-derived state.

```text
NICKSERV REGISTER account-password
NICKSERV IDENTIFY account-password
NICKSERV IDENTIFY alice account-password
NICKSERV LOGOUT
NICKSERV SET PASSWORD new-password
NICKSERV SET EMAIL alice@example.net
NICKSERV VERIFY emailed-token
NICKSERV RESET alice
NICKSERV RESET alice emailed-token new-password
NICKSERV RECOVER alice
NICKSERV RECOVER alice KILL
NICKSERV GHOST alice
NICKSERV HELP
PRIVMSG NickServ :IDENTIFY account-password
```

### ChanServ commands

Ordinary ChanServ functions use an authenticated account. `INFO` is public; `ACCESS` and `SET` require the registered channel's founder account.

```text
CHANSERV INFO #chat
CHANSERV ACCESS #chat ADD alice OWNER
CHANSERV ACCESS #chat ADD bob OP
CHANSERV ACCESS #chat DEL bob
CHANSERV ACCESS #chat LIST
CHANSERV SET #chat MLOCK +nt
CHANSERV SET #chat TOPIC :Welcome to #chat
CHANSERV HELP
PRIVMSG ChanServ :INFO #chat
```

Access roles are `OWNER`, `PROTECTED`, `OP`, `HALFOP`, and `VOICE`. They apply `+q/+o`, `+a/+o`, `+o`, `+h`, and `+v`, respectively, when the account joins or identifies, and are removed on logout.

### MemoServ commands

All MemoServ commands except `HELP` require an identified account. `DELETE` is an alias for `DEL`.

```text
MEMOSERV SEND bob :Please contact me when you return
MEMOSERV LIST
MEMOSERV SENT
MEMOSERV READ 12
MEMOSERV REPLY 12 :Thanks, I saw this
MEMOSERV FORWARD 12 carol
MEMOSERV DEL 12
MEMOSERV DEL ALL
MEMOSERV STATUS
MEMOSERV HELP
PRIVMSG MemoServ :LIST
```

### MODE reference

User-mode examples:

```text
MODE alice
MODE alice +iRw
MODE alice -p
```

Ordinary self-settable modes are `B` bot, `d` suppress ordinary channel messages except configured command-prefix traffic, `i` invisible, `p` hide channels from ordinary `WHOIS`, `R` accept messages only from identified users, `T` reject CTCP, `w` receive wallops, and `x` use the configured cloak. Mode `+x` is applied automatically at registration and may be cleared or reapplied. Modes `g`, `H`, `I`, `s`, and `W` are operator self-modes described in section 1. Other security, transport, service, and authentication modes cannot be self-granted.

Channel-mode examples:

```text
MODE #chat
MODE #chat +nt
MODE #chat +o bob
MODE #chat +b *!*@bad.example
MODE #chat b
MODE #chat +j 3:30
MODE #chat +kl secret-key 50
```

Boolean channel modes are `A` administrator-only join, `c` reject color/control formatting, `i` invite-only, `K` disable KNOCK, `M` identified users only may speak, `m` moderated, `n` no outside messages, `O` operators-only join, `p` private, `R` identified users only may join, `S` strip color/control formatting, `s` secret, `t` topic lock, `T` prohibit channel notices, `V` prohibit invites, and `z` TLS-only. Mode `r` is the service-controlled registered-channel marker.

Membership modes are `q <nick>` owner, `a <nick>` protected, `o <nick>` operator, `h <nick>` halfop, and `v <nick>` voice. List modes are `b <mask>` ban, `e <mask>` ban exception, and `I <mask>` invite exception. Parameter modes are `j <joins:seconds>` join throttle, `k <key>` key, `l <count>` limit, `L <channel>` full-channel redirect, and `B <channel>` banned-client redirect.

### IRCv3 use

ScratchIRCd advertises `account-notify`, `away-notify`, `batch`, `draft/chathistory`, `extended-join`, `labeled-response`, `message-tags`, `sasl=PLAIN`, and `server-time` through CAP 302. Request `sasl`, not `sasl=PLAIN`, and finish negotiation with `CAP END`.

```text
CAP LS 302
CAP REQ :batch draft/chathistory server-time
AUTHENTICATE PLAIN
AUTHENTICATE <base64-frame-1>
AUTHENTICATE <base64-final-frame>
CAP END
JOIN #chat
CHATHISTORY LATEST #chat * 50
```

SASL frames may contain at most 400 characters each and the combined encoded payload may contain at most 800. If the encoded payload is an exact multiple of 400 characters, finish it with `AUTHENTICATE +`. `CHATHISTORY` still requires current channel membership. Operator status alone does not bypass it.

### Identity and idle reporting

Ordinary protocol output uses each client's displayed hostname. Operator `WHOIS` and `USERIP` additionally expose the real security identity. A vhost or cloak changes only the displayed hostname and does not alter ban enforcement against real identity.

A client's reported `WHOIS` idle time resets only when one of its private or channel `PRIVMSG` messages is successfully delivered. No other command or activity resets the messaging-idle timer. Connection liveness is tracked separately, and only an exact matching `PONG` clears an outstanding server challenge.
