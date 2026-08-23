# ScratchIRCd No-Spoof and Client Metadata

Enable the connection checks with:

```text
nospoof = yes
nospoof_timeout_seconds = 30
```

The shipped example enables the feature. Legacy configuration files that do not contain `nospoof` remain disabled until the option is added.

## Handshake

Once a valid NICK is known, ScratchIRCd generates a 128-bit random OpenSSL cookie and sends:

```text
PING :<random-cookie>
:<server> PRIVMSG <nick> :\x01VERSION\x01
```

A matching PONG is required before IRC registration can complete. A stale challenge is rejected when the client next sends protocol traffic after the configured timeout.

The CTCP VERSION reply is optional for registration, but until a VERSION reply is received the client cannot JOIN channels and cannot PRIVMSG ordinary clients. PRIVMSG to IRC operators and network administrators remains available.

## WebIRC

After an authorized WEBIRC command, ScratchIRCd additionally sends:

```text
:<server> PRIVMSG <nick> :\x01WEBSITE\x01
```

If WEBIRC was accepted before NICK, the WEBSITE request is sent immediately after the NICK is established, following the PING and VERSION requests.

## Stored metadata

The Client record stores the returned VERSION and, for WebIRC connections, WEBSITE strings. These are session metadata only and are not persisted to SQLite.

IRC operators and network administrators see successful replies in WHOIS as numerics 672 and 673. Ordinary users do not receive this metadata.
