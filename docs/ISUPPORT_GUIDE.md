# ScratchIRCd ISUPPORT Guide

ScratchIRCd advertises implemented IRC server capabilities through numeric 005 (`RPL_ISUPPORT`). The registration burst uses multiple 005 replies as needed so that no reply exceeds the protocol limit of thirteen ISUPPORT tokens.

## Core identity and naming

```text
CASEMAPPING=rfc1459
CHANTYPES=#&
PREFIX=(qaohv)~&@%+
NETWORK=<configured network_name>
NICKLEN=15
USERLEN=10
HOSTLEN=63
CHANNELLEN=32
TOPICLEN=378
KICKLEN=255
```

`PREFIX` maps founder/protected/operator/halfop/voice membership modes to `~`, `&`, `@`, `%`, and `+` respectively.

## Channel mode classes

```text
CHANMODES=beI,,kljBL,AciKMmnOprRSstTVz
CHANLIMIT=#&:32
MODES=32
EXCEPTS=e
INVEX=I
```

`CHANMODES` intentionally excludes membership modes `q`, `a`, `o`, `h`, and `v` because they are advertised through `PREFIX`.

The four CHANMODES groups are:

```text
Type A: beI
Type B: (none)
Type C: kljBL
Type D: AciKMmnOprRSstTVz
```

Type A modes are list modes. Type C modes take parameters when being set. Type D modes are boolean modes.

ScratchIRCd currently has no enforced maximum number of `+b`, `+e`, or `+I` entries, so `MAXLIST` is deliberately not advertised.

## Command target limits

```text
TARGMAX=PRIVMSG:1,NOTICE:1,JOIN:1,PART:1,KICK:1,NAMES:1
```

These commands currently accept one target at a time.

ScratchIRCd does not yet implement status-prefixed message targets such as `@#channel`, so `STATUSMSG` is deliberately not advertised.

## Presence and history

```text
WATCH=128
SILENCE=64
MSGREFTYPES=timestamp
CHATHISTORY=<configured history_limit>
PCHANNELS=<enabled ChanServ registrations>
```

`PCHANNELS` is a ScratchIRCd-specific extension listing enabled persistent ChanServ channels.

## Accuracy rule

ISUPPORT is treated as a contract with IRC clients. ScratchIRCd should advertise a token only when the corresponding behavior is actually implemented. When a feature is added, its ISUPPORT token should be added together with automated protocol coverage.
