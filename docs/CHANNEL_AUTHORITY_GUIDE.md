# ScratchIRCd Channel Authority Guide

ScratchIRCd uses the following channel membership authority order:

```text
+v  VOICE       rank 1
+h  HALFOP      rank 2
+o  OPERATOR    rank 3
+a  PROTECTED   rank 4
+q  OWNER       rank 5
```

A member may hold more than one privilege bit, but policy decisions use the highest effective authority where rank matters.

## INVITE authority

Normal `/INVITE` authority begins at HALFOP. The following may invite users:

```text
+h  HALFOP
+o  OPERATOR
+a  PROTECTED
+q  OWNER
```

VOICE and ordinary members may not issue channel invitations.

Channel mode `+V` disables `/INVITE` completely. It applies even to OWNER and PROTECTED members. Removing `+V` restores normal authority rules.

## Topic authority

Without channel mode `+t`, any channel member may change the topic.

With `+t`, topic changes require HALFOP or higher:

```text
+h  HALFOP
+o  OPERATOR
+a  PROTECTED
+q  OWNER
```

VOICE and ordinary members may still query the topic but cannot change it while `+t` is active.

## Channel NOTICE policy

Channel mode `+T` rejects all channel NOTICE traffic. The restriction is independent of channel rank: even an OWNER cannot send a channel NOTICE while `+T` is active.

This does not affect `PRIVMSG`; other speaking restrictions such as `+m`, `+M`, `+c`, and user mode `+M` are evaluated separately.

## KICK authority

Normal KICK authority begins at HALFOP. Rank determines which targets may be removed. PROTECTED (`+a`) has additional protection: it cannot be kicked by HALFOP or OPERATOR, but another PROTECTED member or an OWNER may kick it. OWNER remains the highest normal channel authority.

Server-authority commands such as `SAPART` are separate and may bypass ordinary channel hierarchy when the operator has the appropriate server permission.

## User mode +M interaction

Operator-controlled user mode `+M` affects only ordinary channel members. Any channel privilege (`+v`, `+h`, `+o`, `+a`, or `+q`) exempts that client from `+M` in that channel. IRC operators and network administrators are globally exempt.
