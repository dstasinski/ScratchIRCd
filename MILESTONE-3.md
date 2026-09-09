# Milestone 3: MemoServ Completion, E-LINE Exceptions, and Reserved Nicks

Milestone 3 builds on the Milestone 2 release by polishing MemoServ into a complete supported services surface, adding operator-managed E-LINE exceptions for ban and connection-policy bypasses, and adding reserved nickname protection.

The server remains a modern single-server IRC daemon. Multi-server linking remains out of scope.

## Scope

- Complete the MemoServ user and administrator experience.
- Define and implement account lifecycle behavior for stored memos when NickServ accounts are disabled or dropped.
- Add a dedicated MemoServ guide and align the operator, client, and network-administrator documentation.
- Add persistent E-LINE support in the existing bans database.
- Support CIDR-based E-LINE masks for IP-oriented exceptions.
- Use E-LINEs as the general exception mechanism for selected restrictions. Do not add a ZLINE-specific whitelist or separate ZLINE exception list.
- Add reserved nicknames configurable from `ircd.conf`.
- Keep hard-coded service nicknames server-only for everyone.
- Prevent ordinary users from using or registering configured reserved nicknames.
- Add `can_eline` to the operator permission flags.
- Use existing `include/numerics.h` reply macros wherever possible instead of hard-coded numeric literals.
- Preserve the existing real-IP model: KLINE evaluates real hostname/IP identity, ZLINE evaluates `Client.real_ip`, GeoBAN evaluates GeoIP-derived client attributes, and WebIRC policy uses the authenticated end-user address rather than the gateway socket address.

## MemoServ goals

MemoServ is already functional after Milestone 2, including account-to-account persistent memos, SEND, LIST, SENT, READ, REPLY, FORWARD, DEL, STATUS, retention purging, recipient quota checks, sender quota checks, online recipient notification, unread-login notification, and restart lifecycle coverage.

Milestone 3 should finish the remaining polish and lifecycle work.

### MemoServ user-facing polish

- Add a dedicated `docs/MEMOSERV_GUIDE.md`.
- Expand `/MEMOSERV HELP` from a single summary line into command-specific help.
- Use human-readable timestamps in `LIST`, `SENT`, and `READ` output instead of raw Unix timestamps.
- Improve error messages for disabled accounts, unknown accounts, full inboxes, invalid memo IDs, quota exhaustion, and database failures.
- Use existing numeric reply macros from `include/numerics.h` wherever a standard or project-defined numeric already exists.
- Consider user commands such as:
  - `READ NEW` or `READ NEXT`
  - `UNREAD <memo-id>`
  - `LIST NEW`
  - `DEL READ`
  - `DEL <start>-<end>`

### MemoServ lifecycle and policy

- Define exactly what happens to sent and received memos when a NickServ account is disabled.
- Define exactly what happens to sent and received memos when a NickServ account is dropped or force-dropped.
- Decide whether dropped-account senders are preserved, anonymized, or deleted.
- Add integration tests for MemoServ behavior across `NSSET <account> ENABLED 0`, `NSSET <account> ENABLED 1`, `NICKSERV DROP`, and `NSDROP`.
- Add schema-versioning or an explicit schema migration audit for the MemoServ database.
- Add defensive tests for corrupted memo rows, invalid stored accounts, embedded line breaks, embedded NULs, and oversized values.

### MemoServ administration

- Keep `MSINFO` and `MSPURGE` network-administrator-only.
- Document MemoServ quota, sender quota, retention, and database settings.
- Add operator-safe counters where useful without exposing private memo text.
- Ensure admin inspection never leaks memo body content unless a future command explicitly and deliberately allows it.

## E-LINE design

Milestone 3 must add E-LINE support instead of a ZLINE-specific whitelist.

E-LINEs are persistent exceptions stored in the existing `bans.db` database. An E-LINE can exempt a matching user, host, IP, or CIDR mask from selected local ban, GeoBAN, or connection-policy checks.

### Command syntax

```text
/ELINE <user@host-or-IP-or-cidr> <bantypes> <expiry-time> <reason>
```

### Arguments

#### `<user@host-or-IP-or-cidr>`

The mask to exempt.

Recommended operator practice is to use numeric IP masks where possible, especially `*@IP` or `*@CIDR`, such as:

```text
ELINE *@198.51.100.1 kzBG 0 :Trusted user static IP
ELINE *@203.0.113.0/24 zBG 1d :Trusted office CIDR
```

This avoids depending on DNS resolution and allows the exception to apply early in the connection lifecycle.

Supported mask forms should include:

- `user@host`
- `*@host`
- `user@IP`
- `*@IP`
- `user@CIDR`
- `*@CIDR`
- legacy wildcard masks where they fit the existing ban-mask rules

CIDR-based E-LINEs are required. CIDR matching must apply to the host/IP portion of the mask, must support IPv4 and IPv6 CIDR where the underlying IP matching code supports both, and must reject invalid CIDR prefix lengths. For WebIRC clients, IP-oriented E-LINE checks must use the authenticated end-user real IP, not the gateway socket address.

#### `<bantypes>`

A non-empty combination of these letters:

```text
k - KLINE exemption
z - ZLINE exemption
m - max-per-IP / connection-limit exemption
B - DNSBL / blacklist exemption
G - GeoBAN exemption
```

The letters are deliberately case-sensitive for `B` and `G` so they do not conflict visually with lower-case policy flags. Invalid letters must be rejected.

#### `<expiry-time>`

The duration of the exception.

```text
0      permanent
30m    thirty minutes
2h     two hours
1d     one day
1w     one week
```

Expired E-LINEs should be purged or ignored using the same general pattern as timed ban records.

#### `<reason>`

A required operator-supplied reason. Store it without a leading `:` if one is supplied.

Examples:

```text
ELINE *@198.51.100.1 kzBG 0 :Trusted user static IP
ELINE *@203.0.113.0/24 z 1d :Temporary ISP false positive
ELINE *@2001:db8:1200::/48 zBG 1w :Trusted IPv6 office range
ELINE *@192.0.2.50 m 2h :Conference NAT gateway
ELINE *@198.51.100.44 B 30m :DNSBL false positive investigation
ELINE *@203.0.113.77 G 1d :GeoBAN false positive investigation
```

### Removal, listing, and STATS

Add removal and listing forms:

```text
ELINE -<mask>
ELINE LIST
ELINE LIST <bantype>
```

Also add an operator `STATS` selector for E-LINE inspection:

```text
STATS e
```

`STATS e` should list active, unexpired E-LINE exceptions in a compact operator-readable form. It should include enough information for audit work, including mask, bantype, setter, creation time, expiration time, and reason, while preserving the existing `STATS` safety model.

Removal should delete all stored exception rows for the mask unless a later design deliberately adds type-specific removal syntax.

`ELINE LIST <bantype>` should filter by one of `k`, `z`, `m`, `B`, or `G`.

### Permissions

- Add `can_eline` to the operator permission flags and documentation.
- Adding E-LINEs requires `can_eline`.
- Removing E-LINEs should require `can_eline` unless a separate `can_uneline` permission is added deliberately.
- Listing E-LINEs through `ELINE LIST` or `STATS e` should require operator status. A stricter design may require `can_eline`, but ordinary users must not be able to inspect E-LINEs.
- The bootstrap network administrator receives `can_eline` through the existing all-permissions model.
- E-LINE changes should produce ban/server-notice category output so operators can audit exception changes.
- E-LINE command responses and errors should use existing `include/numerics.h` defines wherever possible, with NOTICE fallbacks only where the current command style deliberately uses service/operator notices.

### Database schema

Add this table to the existing bans database:

```sql
CREATE TABLE "exceptions" (
    "type" INTEGER NOT NULL,
    "mask" TEXT NOT NULL COLLATE NOCASE,
    "reason" TEXT NOT NULL DEFAULT '',
    "set_by" TEXT NOT NULL DEFAULT '',
    "created_at" INTEGER NOT NULL DEFAULT (unixepoch()),
    "expires_at" INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY("type","mask")
);
```

Recommended type mapping:

```text
1 = k  KLINE
2 = z  ZLINE
3 = m  max-per-IP / connection limit
4 = B  DNSBL / blacklist
5 = G  GeoBAN
```

The existing bans database schema version must be migrated safely. Existing KLINE/ZLINE rows must survive migration unchanged.

### Evaluation order

E-LINE checks must happen before the corresponding restriction is enforced:

```text
if ELINE matches for k:
    skip KLINE denial
else if KLINE matches:
    deny

if ELINE matches for z:
    skip ZLINE denial
else if ZLINE matches:
    deny

if ELINE matches for m:
    exempt from max-per-IP / connection-limit denial
else enforce configured connection limits

if ELINE matches for B:
    skip DNSBL denial/check result enforcement
else enforce DNSBL result

if ELINE matches for G:
    skip GeoBAN denial
else enforce GeoBAN result
```

E-LINEs must not bypass unrelated security checks, authentication requirements, operator permissions, reserved nickname checks, channel bans, ChanServ policy, SASL policy, TLS certificate handling, malformed input limits, registration syntax rules, or resource safety controls unless that bypass is deliberately added in a future milestone.

### Matching model

- KLINE-oriented E-LINEs should match the same identity surfaces KLINE checks use, such as `user@real_host` and `user@real_ip`.
- ZLINE-oriented E-LINEs should match real IP only, including exact IP, CIDR, and wildcard IP masks.
- Max-per-IP, DNSBL, and GeoBAN E-LINEs should be able to apply early by numeric real IP, preferably via masks such as `*@198.51.100.1`, `*@203.0.113.0/24`, or `*@2001:db8::/32`.
- Hostname-based E-LINEs may not apply until the hostname is known. The implementation must avoid treating unresolved hostnames as a reason to accidentally bypass IP-based checks.
- GeoBAN-oriented E-LINEs exempt a matching client identity from GeoBAN enforcement; they do not disable GeoIP lookup globally and do not bypass unrelated country, region, ASN, or organization reporting.

### Tests required

Add focused integration tests for:

- `ELINE *@ip k 0 :reason` allows a client otherwise matching a KLINE.
- `ELINE *@ip z 0 :reason` allows a client otherwise matching a ZLINE.
- `ELINE *@cidr z 0 :reason` allows a client inside a blocked ZLINE CIDR.
- `ELINE user@cidr z 0 :reason` matches only when both the username and CIDR match.
- IPv6 CIDR E-LINE masks are accepted and matched where IPv6 testing is available.
- `ELINE *@ip B 0 :reason` prevents DNSBL denial while leaving non-exempt clients subject to DNSBL policy.
- `ELINE *@ip m 0 :reason` permits an approved NAT/shared IP case without globally disabling connection limits.
- `ELINE *@ip G 0 :reason` allows a client otherwise denied by GeoBAN while leaving non-exempt clients subject to GeoBAN policy.
- Expired E-LINEs do not exempt clients.
- Invalid bantype letters are rejected.
- Invalid CIDR masks are rejected.
- `ELINE -<mask>` removes the exception and restores normal enforcement.
- `ELINE LIST G` filters GeoBAN exemptions.
- `STATS e` lists active E-LINE exceptions for operators.
- Ordinary users cannot inspect E-LINEs through `STATS e`.
- E-LINE numeric replies use `include/numerics.h` defines wherever applicable instead of hard-coded numeric literals.
- `STATS`, operator guide, and server notices show enough information for operators to audit exceptions without exposing sensitive data unnecessarily.

## Reserved nickname design

Milestone 3 must add configured reserved nicknames from `ircd.conf` while keeping hard-coded service nicknames permanently server-only.

Hard-coded virtual service nicknames are reserved for server use only:

```text
NickServ
ChanServ
MemoServ
```

No client may use or register these service-only names, regardless of case and regardless of whether the client is an ordinary user, IRC operator, or network administrator.

Configured reserved nicknames are names that ordinary users may not use and may not register. IRC operators and network administrators may use or register configured reserved nicknames only when normal server and NickServ rules otherwise allow. Ordinary users attempting to use or register a configured reserved nickname must receive `ERR_RESERVEDNICK`.

### Configuration

Add an `ircd.conf` setting for configured reserved nicknames. The parser should support a comma-separated list.

Preferred form:

```text
reserved_nicks = Admin,Root,OperServ
```

Do not require operators to list `NickServ`, `ChanServ`, or `MemoServ` here. Those names are hard-coded service-only names and are always protected even when the configured list is empty.

### Case handling

Reserved nickname matching must be case-insensitive everywhere:

```text
NickServ
nickserv
NICKSERV
NiCkSeRv
```

all refer to the same service-only reserved nickname. Likewise, if `Admin` appears in `reserved_nicks`, then `Admin`, `admin`, `ADMIN`, and `aDmIn` are the same configured reserved nickname.

Configured reserved nicknames should be stored exactly as written in `ircd.conf` for display and documentation purposes. Matching must use case-insensitive comparison. Case-duplicate configured entries should be silently collapsed, preserving the first spelling encountered.

Example:

```text
reserved_nicks = Admin,admin,ADMIN,Root
```

loads as two configured reserved nicknames: `Admin` and `Root`.

Reserved nicknames must obey the same nickname length and validity constraints used elsewhere in the server. Later, if ScratchIRCd adds a single shared RFC1459 casemapping/canonical-nick helper, reserved nickname matching should move to that helper.

### Enforcement

Reserved nicknames must be enforced in both nickname use and NickServ registration flows:

```text
NICK Admin
NICKSERV REGISTER password
PRIVMSG NickServ :REGISTER password
```

Rules:

- Ordinary users cannot initially register with a configured reserved nick.
- Ordinary users cannot change to a configured reserved nick with `NICK`.
- Ordinary identified users cannot register a configured reserved nick through NickServ.
- IRC operators and network administrators may use configured reserved nicks.
- IRC operators and network administrators may register configured reserved nicks only if the existing NickServ registration rules otherwise allow it.
- Hard-coded service names are always denied to everyone and cannot be registered by anyone.
- Reserved-nick enforcement must not block the virtual service names themselves from receiving service-directed messages.
- Reserved-nick enforcement must not make service names appear as normal users.

### Numeric reply

Attempted use of a service-only nickname by any client, or attempted use of a configured reserved nickname by an unauthorized client, must return `ERR_RESERVEDNICK`.

Use the existing `ERR_RESERVEDNICK` macro from `include/numerics.h`. Do not hard-code the numeric literal at call sites. If the parameter shape needs adjustment, adjust the macro in `include/numerics.h` once and use that define consistently everywhere.

Preferred wire form:

```text
:<server> 484 <nick-or-*> <reserved-nick> :Cannot use reserved nickname
```

The implementation should reuse existing numeric defines from `include/numerics.h` wherever possible across the reserved-nick path. Add or adjust a numeric define only when there is no suitable existing project numeric.

### Tests required

Add focused integration tests for:

- A configured reserved nick is rejected during initial `NICK` registration for ordinary users.
- A configured reserved nick is rejected during later `NICK` changes for ordinary users.
- A configured reserved nick cannot be registered through direct `/NICKSERV REGISTER` by an ordinary user.
- A configured reserved nick cannot be registered through `PRIVMSG NickServ :REGISTER` by an ordinary user.
- A hard-coded service-only nick cannot be used or registered by anyone, including opers and network administrators.
- Rejection uses `ERR_RESERVEDNICK` from `include/numerics.h`, not a hard-coded numeric literal.
- Matching is case-insensitive for service-only and configured reserved nicks.
- Case-duplicate configured reserved nicks are collapsed while preserving the first spelling.
- An IRC operator or network administrator can use a configured reserved nick.
- An IRC operator or network administrator can register a configured reserved nick when normal NickServ requirements are satisfied.
- Reserved service names remain virtual and do not appear as ordinary clients.
- Reload/restart behavior preserves the configured reserved nickname list.

## Numeric reply policy

Milestone 3 implementation work should use `include/numerics.h` defines wherever possible.

Rules:

- Do not hard-code IRC numeric reply literals at call sites when a `numerics.h` macro exists.
- Reserved nickname rejection must use `ERR_RESERVEDNICK` from `include/numerics.h`.
- E-LINE, reserved-nick, MemoServ, and STATS work should reuse existing numeric macros for standard errors such as missing parameters, permission failures, unknown nicknames, and unavailable commands.
- When a new project-specific numeric is needed, add it to `include/numerics.h`, document it, and use the macro everywhere.
- Tests should validate wire output behavior, but implementation code should remain macro-based.

## Out of scope

- No ZLINE-specific whitelist table.
- No separate ZLINE exception command.
- No multi-server propagation.
- No services pseudo-clients joining channels.
- No channel-ban exemptions through E-LINE.
- No bypass of core parser, flood, malformed input, or memory safety protections.
- No dynamic reserved-nick service command in this milestone unless promoted deliberately; reserved nicks come from `ircd.conf`.

## Suggested implementation order

1. Add `MILESTONE-3.md` and document the accepted MemoServ, E-LINE, reserved-nick, and numeric-reply scope.
2. Add `docs/MEMOSERV_GUIDE.md` based on the current MemoServ command set.
3. Define MemoServ account lifecycle behavior and add lifecycle tests.
4. Add reserved nick configuration parsing, `ERR_RESERVEDNICK` reuse, and nickname-use enforcement.
5. Add NickServ reserved-name registration enforcement and tests.
6. Add `can_eline` to the operator permission flags, parser, docs, and tests.
7. Add bans database schema migration for the `exceptions` table.
8. Add E-LINE database APIs and unit tests, including CIDR matching.
9. Add `/ELINE` parser, permissions, server notices, list, remove forms, and `STATS e` inspection.
10. Wire `k`, `z`, `m`, `B`, and `G` evaluation into the correct enforcement points.
11. Add focused integration coverage for E-LINE behavior.
12. Update `docs/OPERATOR_GUIDE.md`, `docs/NETWORK_ADMIN_GUIDE.md`, `docs/CLIENT_GUIDE.md`, `docs/RELEASE_CHECKLIST.md`, `include/numerics.h`, and any relevant config examples.
13. Run focused tests, then full release-gate validation when Milestone 3 is complete.

## Completion gate

Milestone 3 is complete when:

- MemoServ has complete user and administrator documentation.
- MemoServ account disable/drop behavior is defined, implemented, and tested.
- MemoServ persistence and migration behavior is covered by tests.
- Configured reserved nicknames are loaded from `ircd.conf` and enforced for ordinary nickname use and registration.
- Hard-coded service nicknames are server-only and cannot be used or registered by anyone.
- Ordinary users receive `ERR_RESERVEDNICK` when attempting to use or register configured reserved nicks.
- Operators and network administrators can use and register configured reserved nicks when normal rules otherwise allow it.
- Reserved-nick matching is case-insensitive and configured case duplicates are collapsed.
- `can_eline` is available as an operator permission flag and documented.
- E-LINEs are persisted in the bans database and support add, list, remove, expiry, CIDR masks, selected bantypes, and `STATS e` inspection.
- KLINE, ZLINE, max-per-IP, DNSBL, and GeoBAN enforcement correctly honor E-LINEs only for their selected bantypes.
- E-LINEs do not bypass unrelated security, reserved nick, or channel policy controls.
- Milestone 3 command implementations use `include/numerics.h` defines wherever possible instead of hard-coded numeric literals.
- The complete regression suite, focused E-LINE tests, focused reserved-nick tests, focused MemoServ lifecycle tests, sanitizer tests, and a release soak pass before tagging.
