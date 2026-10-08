# ScratchIRCd ChanServ Guide

ChanServ is a virtual server service backed by `data/chanserv.db`. It is not a `Client`, never joins channels, and never appears in NAMES, WHO, ISON, or LUSERS. Channel authority is tied to authenticated NickServ account names rather than the user's current nickname.

## Registration and founder authority

Channel registration and deletion are reserved for network administrators. Those administrative commands are documented in the Network Administrator Guide, not here. Once a network administrator assigns an enabled NickServ account as founder, that founder can manage the ChanServ policies described below. ChanServ is a virtual service and does not join the channel.

## Channel information

```text
/CHANSERV INFO #channel
/PRIVMSG ChanServ :INFO #channel
```

INFO reports the registered channel name, founder account, description, stored mode-lock value, TOPICLOCK state, SecureOps state, successor account, greeting text, and registration time.

## Account access lists

The founder can grant persistent channel privileges to other enabled NickServ accounts:

```text
/CHANSERV ACCESS #channel ADD <account> OWNER
/CHANSERV ACCESS #channel ADD <account> PROTECTED
/CHANSERV ACCESS #channel ADD <account> OP
/CHANSERV ACCESS #channel ADD <account> HALFOP
/CHANSERV ACCESS #channel ADD <account> VOICE
/CHANSERV ACCESS #channel DEL <account>
/CHANSERV ACCESS #channel LIST
```

Access is bound to the authenticated account, not the current nickname. On JOIN or later account identification, the corresponding **single, non-cumulative** membership rank is applied automatically:

- `OWNER` → `+q` (`~` prefix), authority level 5
- `PROTECTED` → `+a` (`&` prefix), authority level 4
- `OP` → `+o` (`@` prefix), authority level 3
- `HALFOP` → `+h` (`%` prefix), authority level 2
- `VOICE` → `+v` (`+` prefix), authority level 1

Higher ranks carry the authority of lower ranks without automatically receiving their mode bits. For an occupied registered channel, ChanServ announces privilege grants through visible MODE messages. Live `ACCESS ADD`, `ACCESS DEL`, and role changes take effect immediately for connected account holders already in the channel. `NICKSERV LOGOUT` removes only the membership privileges supplied by ChanServ; unrelated manually assigned channel modes remain intact.

The founder is implicitly an owner and is not stored as a separate access-list entry.

### Protected members (+a)

Channel membership mode `+a` marks a member as PROTECTED. Its authority is below OWNER (`+q`) and above ordinary OP (`+o`). Only another PROTECTED member or an OWNER may grant/remove `+a` or KICK a protected member. A PROTECTED member may KICK another PROTECTED member; neither PROTECTED nor OP may KICK an OWNER through ordinary KICK.

Ordinary OP/HALFOP users cannot set a channel ban that currently matches a protected member. Ban entries remember whether they were set with PROTECTED/OWNER authority. When a ChanServ PROTECTED or OWNER account reconnects, ordinary bans are ignored for that protected account, while a matching ban deliberately set by PROTECTED/OWNER authority is still enforced. This prevents an OP from bypassing protection by pre-setting a ban before the protected account rejoins.

## Persistent boolean modes and active MLOCK

The founder stores the channel's boolean mode policy with:

```text
/CHANSERV SET #channel MLOCK +nt
```

Supported boolean MLOCK modes are:

```text
A c i K M m n O p R S s t T V z
```

Service-controlled `+r` is restored separately and cannot be placed in MLOCK. Membership privileges such as `+q`, `+a`, `+o`, `+h`, and `+v` are account/member state rather than MLOCK state.

MLOCK is actively enforced. For an enabled registered channel, ordinary MODE and SAMODE requests that would make a boolean mode disagree with the stored MLOCK are rejected with numeric 974. To change the persistent boolean policy, update the MLOCK through ChanServ instead of temporarily changing the live channel.

For example, with `MLOCK +nt`, `MODE #channel -n` and `MODE #channel +m` are rejected, while the stored `+n/+t` state is reapplied when the channel is recreated.

## Persistent parameter modes

The following ordinary channel MODE state is automatically persisted for registered channels:

```text
+k <key>
+l <limit>
+j <joins:seconds>
+L <channel>
+B <channel>
```

Removing any of these modes persists the removal as well. Founders do not need a separate ChanServ command for these settings: an accepted ordinary MODE change updates the live channel and the persistent SQLite snapshot together.

## Persistent ban, exception, and invite-exception lists

Registered channels also persist:

```text
+b <mask>
+e <mask>
+I <mask>
```

Accepted additions and removals are written to `chanserv.db`. The protected-ban authorization flag is stored with `+b` entries, so the PROTECTED/OWNER reconnect semantics survive a daemon restart rather than being lost with the in-memory channel.

## Persistent topic

The founder can store a persistent topic:

```text
/CHANSERV SET #channel TOPIC :Persistent channel topic
```

ChanServ stores the topic text, setter identity, and timestamp in SQLite. When the channel is recreated after becoming empty or after a daemon restart, the topic is restored before JOIN completes so normal topic numerics show the saved value.

The same setting updates the live topic immediately and broadcasts a ChanServ-sourced `TOPIC` event to current members:

```text
/CHANSERV SET #channel TOPIC Welcome to #channel
```

Topic locking can be enabled or disabled without manually using `MODE`:

```text
/CHANSERV SET #channel TOPICLOCK ON
/CHANSERV SET #channel TOPICLOCK OFF
```

`TOPICLOCK ON` applies live `+t`; ordinary members then need channel privilege to change the topic. TOPICLOCK is stored as part of the persistent boolean mode lock and survives restart.

## SecureOps, successor, and greeting

SecureOps audits and reconciles protected channel status (`+q`, `+a`, `+o`, and `+h`) against ChanServ access whenever it is enabled, after joins, and after account login or logout. Unauthorized grants are rejected with numeric `482`. Voice (`+v`) remains ordinary channel state and is not restricted by SecureOps.

```text
/CHANSERV SET #channel SECUREOPS ON
/CHANSERV SET #channel SECUREOPS OFF
```

The founder may record an existing NickServ account as successor, or clear it. The successor must be an enabled account other than the founder.

```text
/CHANSERV SET #channel SUCCESSOR GraceAccount
/CHANSERV SET #channel SUCCESSOR NONE
```

If a founder account is deleted with `NSDROP` or disabled with `NSSET <account> ENABLED 0`, every enabled channel founded by that account is reconciled immediately. When the channel has a valid enabled successor, the successor becomes the new founder, the successor field is cleared, the live channel cache is refreshed, and member privileges are reconciled. When there is no valid successor, the channel registration is disabled rather than deleted, the live channel loses service-controlled `+r`, and current members receive a ChanServ-sourced registration-mode broadcast:

A disabled registration whose stored founder account no longer exists, or is still disabled, cannot be re-enabled directly. A network administrator must first assign an existing enabled founder with `CSSET #channel FOUNDER <NickServ-account>`, then run `CSSET #channel ENABLED 1`.

A temporary `NSSET <account> ENABLED 0` keeps that account's non-founder ChanServ access and successor references intact so an administrator can restore the account later. A permanent `NSDROP <account>` removes that account from ChanServ access lists and clears any successor references to it. This prevents re-registering the same account name from unexpectedly recovering old channel privileges or inheritance rights.

A greeting is sent by ChanServ to each client after a successful join. The greeting is a notice from ChanServ and does not make ChanServ join the channel.

```text
/CHANSERV SET #channel GREETING :Welcome to the main channel.
/CHANSERV SET #channel GREETING NONE
```

SecureOps, successor, and greeting settings are stored in SQLite and survive daemon restart.

## Persistence and founder privileges

Channel registrations, access lists, MLOCK/TOPICLOCK state, topic data, SecureOps, successor, greeting, parameter modes, and `+b/+e/+I` lists survive daemon restart in SQLite even when the in-memory channel becomes empty and is reclaimed. When the channel is later recreated by JOIN, ScratchIRCd restores the complete persistent policy before normal JOIN restrictions are evaluated.

This means a stored key or ban is effective on the first JOIN after restart, rather than being applied only after someone has already entered the channel.

## Scope

This guide covers ChanServ commands and founder-managed channel policy. Network administrator registration, deletion, account administration, and server configuration are covered in the Network Administrator Guide.
