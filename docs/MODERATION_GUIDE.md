# ScratchIRCd DEAF and MUTE Moderation Controls

ScratchIRCd 0.29 adds two operator-controlled client modes. These are moderation state, not user preferences: ordinary clients cannot set or clear them with MODE.

## DEAF: user mode +D

An IRC operator or network administrator may set or clear DEAF with:

```text
DEAF +<nick>
DEAF -<nick>
```

A client with user mode `+D` cannot exchange direct `PRIVMSG` or `NOTICE` traffic with ordinary users. The exception is IRC operators and network administrators: a `+D` client may send private traffic to an oper, and an oper may send private traffic to a `+D` client.

When an ordinary user sends a `PRIVMSG` to a `+D` client, the message is not delivered and the sender receives:

```text
:<recipient>!<user>@<display_host> NOTICE <sender> :I cannot send or receive private messages.
```

An ordinary `NOTICE` sent to a `+D` client is silently discarded to avoid NOTICE-response loops. Outbound `PRIVMSG` and `NOTICE` from a `+D` client to a non-oper are also discarded.

`+D` affects direct client-to-client messaging only. Virtual service commands sent to NickServ, ChanServ, or MemoServ remain available because those services are not Client records.

Clients cannot use either of these to change the mode themselves:

```text
MODE <self> +D
MODE <self> -D
```

ScratchIRCd replies with `481 ERR_NOPRIVILEGES`. `MODE <self>` still reports `D` while the mode is active.

## MUTE: user mode +M

An IRC operator or network administrator may set or clear MUTE with:

```text
MUTE +<nick>
MUTE -<nick>
```

A client with user mode `+M` cannot send to any channel. The restriction applies regardless of voice, halfop, operator, protected, or owner membership status. In effect, channel speaking is denied before normal channel privilege checks are considered.

Both channel `PRIVMSG` and channel `NOTICE` are rejected with numeric `404 ERR_CANNOTSENDTOCHAN` while `+M` is active.

`+M` does not prevent direct client-to-client messages; that is the separate purpose of `+D`.

Clients cannot use either of these to change the mode themselves:

```text
MODE <self> +M
MODE <self> -M
```

ScratchIRCd replies with `481 ERR_NOPRIVILEGES`. `MODE <self>` reports `M` while the mode is active.

## Distinction from existing modes

Upper-case user mode `+D` is unrelated to lower-case user mode `+d`. Existing `+d` suppresses ordinary channel PRIVMSG delivery to the client while permitting configured command-prefix traffic such as `!command`; `+D` controls direct private traffic.

Upper-case user mode `+M` is also distinct from channel mode `+M`. Channel `+M` requires a registered nickname to speak in that particular channel. User `+M` prevents the client from speaking in every channel.
