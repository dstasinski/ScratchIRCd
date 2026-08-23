# ScratchIRCd STATS Guide

ScratchIRCd is intentionally a single-server daemon, so `/STATS` exposes only statistics that correspond to real local state. It does not emulate server-link, routing-class, or inter-server statistics.

## Supported selectors

```text
STATS
STATS ?
STATS h
```

Displays the supported selector list using numeric 210 and ends with numeric 219.

```text
STATS u
```

Displays daemon uptime using numeric 242. This selector is available to registered users.

```text
STATS k
```

Lists active persistent KLINE records from `data/bans.db`. This selector is restricted to authenticated IRC operators and network administrators. Expired timed KLINE records are purged before listing. KLINE entries use numeric 216 and include the mask, setter, and reason.

```text
STATS z
```

Lists active persistent ZLINE records from `data/bans.db`. This selector is restricted to authenticated IRC operators and network administrators. Expired timed ZLINE records are purged before listing. Entries are emitted as numeric 210 informational STATS rows and include the mask, setter, expiration timestamp (`0` means permanent), and reason.

```text
STATS g
```

Lists active persistent GeoBAN policies from the `geo_bans` table in `data/bans.db`. This selector is restricted to authenticated IRC operators and network administrators. Entries are emitted as numeric 210 informational STATS rows and include policy type (`COUNTRY`, `REGION`, `ASN`, or `ORG`), normalized value, setter, expiration timestamp (`0` means permanent), and reason.

All completed STATS reports end with numeric 219. Ordinary users attempting `STATS k`, `STATS z`, or `STATS g` receive numeric 481 and no security-policy data.

## Deliberately unsupported selectors

ScratchIRCd does not currently implement traditional STATS selectors for server links, routing classes, inter-server configuration, command counters, memory accounting, or traffic accounting. Those selectors are omitted rather than populated with misleading placeholder data. If ScratchIRCd later gains trustworthy local counters that are useful operationally, they can be added without introducing multi-server architecture.
