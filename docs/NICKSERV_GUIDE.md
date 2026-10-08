# ScratchIRCd NickServ Guide

NickServ is a virtual service. It is addressable as `NickServ`, but it is not a real IRC client: it never joins channels and does not appear in NAMES, WHO, ISON, or LUSERS.

NickServ account commands may be sent as IRC client slash commands:

```text
/NICKSERV <command> [parameters]
```

Or through the traditional service-message form:

```text
/PRIVMSG NickServ :<command> [parameters]
```

IRC clients may translate `/NICKSERV` into a service message. ScratchIRCd accepts both forms.

## REGISTER

```text
/NICKSERV REGISTER <password>
```

Registers the current nickname as an account and immediately identifies the current connection to it. Passwords are stored only as Argon2id hashes in `data/nickserv.db`.

## IDENTIFY

```text
/NICKSERV IDENTIFY <password>
/NICKSERV IDENTIFY <account> <password>
/IDENTIFY <password>
/IDENTIFY <account> <password>
```

Authenticates the current connection to a registered account. Successful identification sets user mode `+r` and stores the authenticated account name separately from the current nickname.

If the account has a NickServ vhost, identification changes only `display_host` and sets `+t`; `real_ip` and `real_host` are never changed.

## LOGOUT

```text
/NICKSERV LOGOUT
```

Detaches the authenticated account from the current connection and removes service-controlled `+r`. ChanServ privileges supplied by that account are removed immediately from joined channels. IRCv3 peers that negotiated `account-notify` receive `ACCOUNT *`.

If the displayed hostname came from the NickServ account, logout removes `+t` and restores the server cloak. An operator or `SETHOST` vhost that superseded the account vhost is preserved.

## SET PASSWORD

```text
/NICKSERV SET PASSWORD <new-password>
```

Requires identification. Replaces the stored Argon2id password hash and invalidates any outstanding password-reset token.

## SET EMAIL and VERIFY

```text
/NICKSERV SET EMAIL <address>
/NICKSERV VERIFY <token>
```

`SET EMAIL` requires identification. ScratchIRCd does not immediately trust the supplied address. It creates a random verification token, stores only the token's SHA-256 hash, and sends the plaintext token to the supplied address. The address becomes eligible for password recovery only after `VERIFY` succeeds.

Verification and reset tokens use seven-digit numeric codes formatted as `XXX-XXXX` (the separator may be omitted when entering a code). Requests are rate-limited to protect accounts against abuse. Verification tokens expire after `nickserv_verify_seconds` (default 86400 seconds / 24 hours).

Email delivery requires a sendmail-compatible local MTA configured by the network administrator. If mail is disabled, `SET EMAIL` reports that email services are unavailable.

## Password reset by email

Request a reset:

```text
/NICKSERV RESET <account>
```

ScratchIRCd always returns the same generic response whether or not the account exists or has a verified email address. This prevents the command from being used to enumerate accounts or email configuration.

If the account exists, is enabled, and has a verified email address, a random one-time token is mailed to it. Only the SHA-256 hash of that token is stored in `data/nickserv.db`.

Complete the reset:

```text
/NICKSERV RESET <account> <token> <new-password>
```

A successful reset consumes the token and stores a new Argon2id password hash. Reset tokens expire after `nickserv_reset_seconds` (default 1800 seconds / 30 minutes) and may be used only once.

## RECOVER

Default recovery safely frees a registered nickname without disconnecting its current occupant:

```text
/NICKSERV RECOVER <nick>
```

The requester must already be identified to the account whose name matches `<nick>`. If `<nick>` is occupied by another connection, ScratchIRCd forcibly renames that connection to a generated `Guest<connection-id>` nickname. The recovering user may then issue the normal:

```text
/NICK <nick>
```

command to take the freed nickname.

To disconnect the occupying session instead of renaming it:

```text
/NICKSERV RECOVER <nick> KILL
```

This is a NickServ account-owner action and does not require IRC-operator `can_kill` permission.

## GHOST

```text
/NICKSERV GHOST <nick>
```

`GHOST` is deliberately an alias for the KILL form of RECOVER. The requester must be identified to the corresponding registered account. The occupying connection is disconnected.

## HELP

```text
/NICKSERV HELP
```

Displays the implemented NickServ command families.

