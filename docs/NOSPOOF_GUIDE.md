# ScratchIRCd No-Spoof and Client Metadata

Enable the connection checks with:

```text
nospoof = yes
nospoof_timeout_seconds = 30
```

The shipped example enables the feature. Legacy configuration files that do not contain `nospoof` remain disabled until the option is added.

## Handshake

Once a valid NICK is known, ScratchIRCd generates a 128-bit random OpenSSL cookie and sends only:

```text
PING :<random-cookie>
```

A matching PONG is required before IRC registration can complete. A stale challenge is rejected when the client next sends protocol traffic after the configured timeout.

Only after the correct PONG is received does ScratchIRCd send:

```text
:<server> PRIVMSG <nick> :\x01VERSION\x01
```

The CTCP VERSION reply is optional for registration, but until a VERSION reply is received the client cannot JOIN channels and cannot PRIVMSG ordinary clients. PRIVMSG to IRC operators and network administrators remains available.

## WebIRC

An authenticated WebIRC connection is queried with CTCP WEBSITE only after the no-spoof PING cookie has been verified:

```text
:<server> PRIVMSG <nick> :\x01WEBSITE\x01
```

If WEBIRC authentication occurs before the PONG, WEBSITE is deferred until the PONG succeeds. If WEBIRC authentication occurs after a successful PONG, WEBSITE is sent immediately after WEBIRC is accepted. Thus VERSION and WEBSITE are never sent to a connection that has not passed the no-spoof check.

## Stored metadata

The Client record stores the returned VERSION and, for WebIRC connections, WEBSITE strings. These are session metadata only and are not persisted to SQLite.

IRC operators and network administrators see successful replies in WHOIS as numerics 672 and 673. Ordinary users do not receive this metadata.
