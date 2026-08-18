# ScratchIRCd Network Administrator Guide

This guide documents the commands and responsibilities available to the ScratchIRCd network administrator. It will be kept current as commands are implemented.

## Bootstrap network administrator

The network administrator is the only operator identity stored in `ircd.conf`. Ordinary IRC operators are stored in `operators.db`.

Configuration keys:

```text
operators_db = operators.db
netadmin_name = root
netadmin_password_hash = $argon2id$...
netadmin_hostmask = *!*@*
netadmin_vhost = admin.example.net
```

Generate the Argon2id password hash with:

```sh
./build/scratchircd-mkpasswd 'your password'
```

Authenticate through IRC with:

```text
OPER root <password>
```

A successful bootstrap login receives network-administrator status (`+N`) and the complete operator permission set.

## Operator database management

Ordinary operators are stored only in the SQLite `operators` table in `operators.db`. Passwords supplied through IRC are immediately converted to Argon2id hashes; plaintext passwords are not stored.

### OPERADD

```text
OPERADD <name> <password> <vhost|-> :<permissions|->
```

Creates and enables an operator. `-` means no vhost or no permissions.

Example:

```text
OPERADD helper secret staff.example.net :can_kill,can_kline,get_host
```

The `netadmin` permission is forbidden for database operators.

### OPERDEL

```text
OPERDEL <name>
```

Deletes an operator record.

### OPERSET

```text
OPERSET <name> NAME <newname>
OPERSET <name> PASSWORD <newpassword>
OPERSET <name> PERMISSIONS :<permissions|->
OPERSET <name> VHOST <vhost|->
OPERSET <name> ENABLED <0|1>
```

Edits an existing operator. Updates change `updated_at`; `created_at` remains unchanged.

### OPERLIST

```text
OPERLIST
OPERLIST <name>
```

Lists operator metadata without revealing password hashes.

## Operator permissions

The current permission names are:

- `can_rehash` — permission for REHASH when implemented.
- `can_die` — permission for DIE when implemented.
- `can_restart` — permission for RESTART when implemented.
- `helpop` — grants user mode `+h` on OPER login.
- `can_wallops` — permission to send WALLOPS when implemented.
- `can_kill` — permission for KILL when implemented.
- `can_kline` — permission to add KLINEs when implemented.
- `can_unkline` — permission to remove KLINEs when implemented.
- `can_zline` — permission for ZLINE when implemented.
- `get_host` — permits the configured operator vhost to be applied and grants `+t`.
- `can_override` — reserved for SAJOIN, SAPART, SAMODE, SETHOST, SETIDENT and related override commands when implemented.
- `netadmin` — reserved exclusively for the bootstrap network administrator and cannot be assigned to database operators.

## Commands currently available to the network administrator

The network administrator may use all general client commands and all currently implemented operator commands. Administrator-specific commands currently implemented are:

```text
OPER
OPERADD
OPERDEL
OPERSET
OPERLIST
```

The following requested administrator/operator commands are planned but are not yet implemented:

```text
KILL
KLINE
REHASH
RESTART
SAJOIN
SAMODE
SAPART
SETHOST
SETIDENT
SETNAME
WALLOPS
ZLINE
```

`DIE` is represented by the `can_die` permission but is not currently in the original standard-command list and is not implemented.

## General commands also available

```text
ADMIN
AWAY
INVITE
ISON
JOIN
KICK
LIST
LUSERS
MODE
MOTD
NAMES
NICK
NOTICE
PART
PASS
PING
PONG
PRIVMSG
QUIT
RULES
TOPIC
USER
USERHOST
USERIP
WHO
WHOIS
```

Some planned general commands, including IDENTIFY, SILENCE, WATCH and WHOWAS, are not implemented yet.

## Security notes

`ircd.conf`, `operators.db`, SQLite journal/WAL files, and the build directory are ignored by Git. Keep the bootstrap password hash and live database out of source control. The network administrator hostmask is checked against the effective IRC client identity, which is also the identity that will be used for authenticated WebIRC clients.
