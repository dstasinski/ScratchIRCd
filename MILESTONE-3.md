# Milestone 3: MemoServ Completion and E-LINE Exceptions

Milestone 3 builds on the Milestone 2 release by polishing MemoServ into a complete supported services surface and adding operator-managed E-LINE exceptions for ban and connection-policy bypasses.

The server remains a modern single-server IRC daemon. Multi-server linking remains out of scope.

## Scope

- Complete the MemoServ user and administrator experience.
- Define and implement account lifecycle behavior for stored memos when NickServ accounts are disabled or dropped.
- Add a dedicated MemoServ guide and align the operator, client, and network-administrator documentation.
- Add persistent E-LINE support in the existing bans database.
- Use E-LINEs as the general exception mechanism for selected restrictions. Do not add a ZLINE-specific whitelist or separate ZLINE exception list.
- Preserve the existing real-IP model: KLINE evaluates real hostname/IP identity, ZLINE evaluates `Client.real_ip`, GeoBAN evaluates GeoIP-derived client attributes, and WebIRC policy uses the authenticated end-user address rather than the gateway socket address.

## MemoServ goals

MemoServ is already functional after Milestone 2, including account-to-account persistent memos, SEND, LIST, SENT, READ, REPLY, FORWARD, DEL, STATUS, retention purging, recipient quota checks, sender quota checks, online recipient notification, unread-login notification, and restart lifecycle coverage.

Milestone 3 should finish the remaining polish and lifecycle work.

### MemoServ user-facing polish

- Add a dedicated `docs/MEMOSERV_GUIDE.md`.
- Expand `/MEMOSERV HELP` from a single summary line into command-specific help.
- Use human-readable timestamps in `LIST`, `SENT`, and `READ` output instead of raw Unix timestamps.
- Improve error messages for disabled accounts, unknown accounts, full inboxes, invalid memo IDs, quota exhaustion, and database failures.
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

E-LINEs are persistent exceptions stored in the existing `bans.db` database. An E-LINE can exempt a matching user, host, or IP mask from selected local ban, GeoBAN, or connection-policy checks.

### Command syntax

```text
/ELINE <user@host-or-IP-or-cidr> <bantypes> <expiry-time> <reason>
```

### Arguments

#### `<user@host-or-IP-or-cidr>`

The mask to exempt.

Recommended operator practice is to use numeric IP masks where possible, especially `*@IP`, such as:

```text
ELINE *@198.51.100.1 kzBG 0 :Trusted user static IP
```

This avoids depending on DNS resolution and allows the exception to apply early in the connection lifecycle.

Supported mask forms should include:

- `user@host`
- `*@host`
- `user@IP`
- `*@IP`
- `*@CIDR`
- legacy wildcard masks where they fit the existing ban-mask rules

CIDR matching should apply to the host/IP portion of the mask. For WebIRC clients, IP-oriented E-LINE checks must use the authenticated end-user real IP, not the gateway socket address.

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

- Adding E-LINEs requires an explicit operator permission, preferably `can_eline`.
- Removing E-LINEs should require the same permission unless a separate `can_uneline` permission is added deliberately.
- Listing E-LINEs through `ELINE LIST` or `STATS e` should require operator status. A stricter design may require `can_eline`, but ordinary users must not be able to inspect E-LINEs.
- The bootstrap network administrator receives the permission through the existing all-permissions model.
- E-LINE changes should produce ban/server-notice category output so operators can audit exception changes.

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

E-LINEs must not bypass unrelated security checks, authentication requirements, operator permissions, channel bans, ChanServ policy, SASL policy, TLS certificate handling, malformed input limits, registration syntax rules, or resource safety controls unless that bypass is deliberately added in a future milestone.

### Matching model

- KLINE-oriented E-LINEs should match the same identity surfaces KLINE checks use, such as `user@real_host` and `user@real_ip`.
- ZLINE-oriented E-LINEs should match real IP only, including exact IP, CIDR, and wildcard IP masks.
- Max-per-IP, DNSBL, and GeoBAN E-LINEs should be able to apply early by numeric real IP, preferably via masks such as `*@198.51.100.1` or `*@203.0.113.0/24`.
- Hostname-based E-LINEs may not apply until the hostname is known. The implementation must avoid treating unresolved hostnames as a reason to accidentally bypass IP-based checks.
- GeoBAN-oriented E-LINEs exempt a matching client identity from GeoBAN enforcement; they do not disable GeoIP lookup globally and do not bypass unrelated country, region, ASN, or organization reporting.

### Tests required

Add focused integration tests for:

- `ELINE *@ip k 0 :reason` allows a client otherwise matching a KLINE.
- `ELINE *@ip z 0 :reason` allows a client otherwise matching a ZLINE.
- `ELINE *@cidr z 0 :reason` allows a client inside a blocked ZLINE CIDR.
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
- `STATS`, operator guide, and server notices show enough information for operators to audit exceptions without exposing sensitive data unnecessarily.

## Out of scope

- No ZLINE-specific whitelist table.
- No separate ZLINE exception command.
- No multi-server propagation.
- No services pseudo-clients joining channels.
- No channel-ban exemptions through E-LINE.
- No bypass of core parser, flood, malformed input, or memory safety protections.

## Suggested implementation order

1. Add `MILESTONE-3.md` and document the accepted MemoServ and E-LINE scope.
2. Add `docs/MEMOSERV_GUIDE.md` based on the current MemoServ command set.
3. Define MemoServ account lifecycle behavior and add lifecycle tests.
4. Add bans database schema migration for the `exceptions` table.
5. Add E-LINE database APIs and unit tests.
6. Add `/ELINE` parser, permissions, server notices, list, remove forms, and `STATS e` inspection.
7. Wire `k`, `z`, `m`, `B`, and `G` evaluation into the correct enforcement points.
8. Add focused integration coverage for E-LINE behavior.
9. Update `docs/OPERATOR_GUIDE.md`, `docs/NETWORK_ADMIN_GUIDE.md`, `docs/RELEASE_CHECKLIST.md`, and any relevant config examples.
10. Run focused tests, then full release-gate validation when Milestone 3 is complete.

## Completion gate

Milestone 3 is complete when:

- MemoServ has complete user and administrator documentation.
- MemoServ account disable/drop behavior is defined, implemented, and tested.
- MemoServ persistence and migration behavior is covered by tests.
- E-LINEs are persisted in the bans database and support add, list, remove, expiry, selected bantypes, and `STATS e` inspection.
- KLINE, ZLINE, max-per-IP, DNSBL, and GeoBAN enforcement correctly honor E-LINEs only for their selected bantypes.
- E-LINEs do not bypass unrelated security or channel policy controls.
- The complete regression suite, focused E-LINE tests, focused MemoServ lifecycle tests, sanitizer tests, and a release soak pass before tagging.
