# ScratchIRCd User Modes Guide

ScratchIRCd stores user modes in each connected `Client`. Modes affect presentation and policy only as documented here; security identity remains in `real_ip` and `real_host`, while public IRC output uses `display_host`.

## Behavioral modes completed in 0.24

### `+d` — deaf to ordinary channel PRIVMSG

`MODE <nick> +d` suppresses ordinary channel `PRIVMSG` delivery to that client. Channel messages beginning with a command prefix listed in `IRCD_DEAF_COMMAND_PREFIXES` still pass through; the current default is `!`. Channel `NOTICE` is not suppressed by `+d`.

### `+H` — hide IRC operator status

IRC operators may self-toggle `+H`. When enabled, ordinary users do not receive the IRCop/administrator WHOIS line. Other IRC operators may still see it.

### `+I` — hide IRC operator idle time

IRC operators may self-toggle `+I`. When enabled, ordinary users do not receive the WHOIS idle/signon numeric for that operator. The operator themself and other IRC operators may still see it.

### `+W` — WHOIS notification

IRC operators may self-toggle `+W`. When another client performs WHOIS on them, ScratchIRCd sends the operator a server NOTICE naming the requester using only the requester's public `nick!user@display_host` identity.

### `+x` — cloaked displayed hostname

Any registered user may self-toggle `+x`. Enabling it replaces only `display_host` with a deterministic `cloak-...` value derived from the server/network identity and the client's verified real hostname or IP. `real_ip` and `real_host` are unchanged and remain available for KLINE, ZLINE, DNSBL, GeoIP, and operator inspection.

`+x` and `+t` are alternate sources for `display_host`. Enabling `+x` clears `+t`; applying a NickServ/operator/SETHOST vhost clears `+x`. Removing `+x` restores `display_host` from `real_host` when FCrDNS succeeded, otherwise from `real_ip`.

## Operator listener modes completed in 0.25

### `+g` — GLOBOPS/LOCOPS

Only IRC operators may self-toggle `+g`. A client with `+g` receives `GLOBOPS` and `LOCOPS` messages. Sending either command also requires IRC operator status and `+g`.

ScratchIRCd is intentionally single-server, so both commands are local-process broadcasts. They remain separate command names for client compatibility and clear policy semantics.

```text
MODE <nick> +g
GLOBOPS :message for +g operators
LOCOPS :local operator message
```

### `+s` — server notices

Only IRC operators may self-toggle `+s`. A `+s` operator receives daemon-generated notices for meaningful administrative/security events such as KILL, KLINE, and ZLINE actions.

```text
MODE <nick> +s
```

The notice prefix uses the configured server name and does not expose another client's hidden real identity unless the administrative command itself already contains that security value.

## Channel color modes completed in 0.25

Channel mode `+c` rejects channel text containing IRC color controls or ANSI escape color sequences. For `PRIVMSG`, the sender receives the normal cannot-send numeric; `NOTICE` rejection remains silent in accordance with NOTICE semantics.

Channel mode `+S` strips IRC color codes and ANSI SGR color sequences before delivery. Persistent history stores the stripped text, so replay matches what channel members originally saw.

When both `+c` and `+S` are present, `+c` wins: colored input is rejected rather than transformed.

## Other implemented user modes

- `+B` — bot marker; shown in WHOIS.
- `+h` — HelpOp, granted through operator permissions.
- `+i` — invisible in general WHO results.
- `+N` — network administrator; server-controlled.
- `+o` — IRC operator; granted by OPER.
- `+p` — hide channel membership from WHOIS where visibility policy requires it.
- `+R` — accept direct PRIVMSG/NOTICE only from registered (`+r`) users.
- `+r` — authenticated NickServ account; service-controlled.
- `+S` — services marker; server/service-controlled.
- `+T` — reject direct CTCPs.
- `+t` — vhost is active; changes only `display_host`.
- `+V` — authenticated WebIRC client; server-controlled.
- `+w` — receive WALLOPS.
- `+z` — secure TLS transport; server-controlled.

Security-derived modes such as `+N`, `+o`, `+r`, `+S`, `+t`, `+V`, and `+z` cannot be manufactured through ordinary MODE.
