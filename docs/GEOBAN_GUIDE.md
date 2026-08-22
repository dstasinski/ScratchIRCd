# ScratchIRCd GeoBAN Guide

GeoBAN provides persistent server-wide connection policy based on MaxMind-enriched client metadata. GeoBAN is deliberately separate from KLINE and ZLINE: KLINE continues to match real user/hostname/IP identity, while ZLINE continues to match real numeric IP addresses.

GeoBAN policies are stored in the existing `data/bans.db` database in a separate `geo_bans` table. Each row stores policy type, normalized value, reason, setter, creation time, and optional expiration time. `expires_at = 0` means permanent. Expired rows are ignored and purged automatically.

Operators require the `can_geoban` permission. The bootstrap network administrator receives it through the complete permission set.

## Commands

Add a policy:

```text
GEOBAN <COUNTRY|REGION|ASN|ORG> <value> <duration|0> [:reason]
```

List active policies:

```text
GEOBAN LIST
```

Remove a policy:

```text
UNGEOBAN <COUNTRY|REGION|ASN|ORG> <value>
```

Durations accept `s`, `m`, `h`, `d`, and `w`. `0`, `permanent`, `perm`, and `forever` mean permanent. Temporary policies retain their original expiration time across daemon restarts.

## COUNTRY

Country values are two-letter MaxMind ISO country codes. They are normalized to uppercase and matched case-insensitively.

```text
GEOBAN COUNTRY RU 0 :Connections from this country are not accepted
UNGEOBAN COUNTRY RU
```

## REGION

Region values match MaxMind subdivision ISO codes such as US state or Canadian province codes. Values are normalized to uppercase and matched case-insensitively.

```text
GEOBAN REGION AZ 7d :Temporary regional restriction
```

## ASN

ASN values may be supplied with or without the `AS` prefix. ScratchIRCd normalizes them to the numeric ASN copied from MaxMind.

```text
GEOBAN ASN 22773 1d :Network abuse
GEOBAN ASN AS22773 1d :Network abuse
```

Both examples create the same policy value: `22773`.

## ORG

Organization policies match MaxMind `autonomous_system_organization`. Matching is case-insensitive and uses Tcl-style glob semantics: `*`, `?`, bracket character classes, and backslash escaping are supported.

Values containing spaces should use braces:

```text
GEOBAN ORG {Example Network LLC} 0 :Blocked provider
GEOBAN ORG {*Example Network*} 0 :Blocked provider family
```

Braces are command grouping syntax and are not stored in the database.

## Enforcement

GeoIP enrichment is completed from the finalized `Client.real_ip` before GeoBAN evaluation. For authenticated WebIRC connections this is the actual end-user IP, never the gateway IP.

A matching GeoBAN is checked before registration completes. The client receives the normal banned-client numeric and is disconnected using the policy reason. Adding a new policy also disconnects already-registered matching clients, except the operator who is setting the policy.

GeoBAN does not inspect or modify `display_host`, KLINE masks, ZLINE masks, channel bans, cloaks, or vhosts.
