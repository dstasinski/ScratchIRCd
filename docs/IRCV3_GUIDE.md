# ScratchIRCd IRCv3 Guide

ScratchIRCd 0.16 introduces a general capability bitset and capability registry rather than treating SASL as a one-off client flag.

## CAP negotiation

The server currently advertises:

```text
account-notify sasl
```

Clients may negotiate capabilities before registration:

```text
CAP LS 302
CAP REQ :account-notify sasl
NICK example
USER example 0 * :Example User
CAP END
```

Registration remains held while CAP negotiation is active. `CAP END` releases the registration hold once the ordinary registration prerequisites are satisfied.

`CAP LIST` returns the capabilities currently enabled for that connection.

`CAP REQ` accepts multiple space-separated capability names and supports removal with a leading `-`:

```text
CAP REQ :account-notify sasl
CAP REQ :-sasl
```

Requests are atomic. If any requested capability is unknown, the server sends `NAK` and does not apply any part of that request.

## sasl

The `sasl` capability enables the existing `AUTHENTICATE PLAIN` flow. SASL uses the NickServ account database and therefore produces the same authenticated account, `+r`, and account vhost state as NickServ IDENTIFY.

See `docs/NICKSERV_GUIDE.md` for account management details.

## account-notify

A client that enables `account-notify` receives IRCv3 `ACCOUNT` messages when another registered client sharing a channel becomes authenticated after registration:

```text
:nick!user@display.host ACCOUNT accountname
```

Only clients that negotiated `account-notify` receive these messages. A recipient sharing more than one channel with the account-changing user receives one notification rather than one notification per shared channel.

Account state established by SASL before registration does not generate an `ACCOUNT` notification because the client has not yet become visible to channel peers.

The notification path covers all current post-registration NickServ authentication entry points: direct `IDENTIFY`, direct `NICKSERV ...`, and `PRIVMSG NickServ :...`.

## Development direction

The capability registry is the foundation for additional IRCv3 features. New capabilities should be represented by a capability bit, advertised through the CAP registry, and used to gate capability-specific protocol output. This avoids adding independent boolean fields and one-off negotiation logic for every future IRCv3 feature.
