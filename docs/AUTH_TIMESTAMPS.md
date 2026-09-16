# ScratchIRCd Authentication Timestamps

ScratchIRCd persists the last successful account and operator authentication times so operators can audit recent successful logins without exposing passwords, password hashes, reset tokens, memo text, or private message content.

## Stored fields

`nickserv.db` stores the last successful NickServ identification time in the `nickserv_accounts.last_identified_at` column. The value is a Unix epoch timestamp and defaults to `0`, which means the account has never successfully identified.

`operators.db` stores the last successful IRC operator authentication time in the `operators.last_opered_at` column. The value is a Unix epoch timestamp and defaults to `0`, which means the operator record has never successfully authenticated with `/OPER`.

ScratchIRCd has not yet been released, so these databases are treated as current-schema development databases. Do not depend on pre-release database shapes being upgraded in place; delete or recreate development databases when the schema changes. See [`DATABASE_SCHEMA_POLICY.md`](DATABASE_SCHEMA_POLICY.md) for the broader pre-release database policy.

## What updates the fields

NickServ updates `last_identified_at` after a successful account identification. Successful account registration also stores the registration time because registration immediately identifies the new account.

Operator authentication updates `last_opered_at` after a successful `/OPER` against a row in `operators.db`.

Failed NickServ identifies and failed `/OPER` attempts do not update these fields.

The bootstrap network administrator configured in `ircd.conf` does not have a row in `operators.db`, so its `/OPER` success is not stored in `operators.last_opered_at`.

## Operator-visible lookups

Any authenticated IRC operator or network administrator can inspect the last successful authentication timestamps through `STATS`:

```text
STATS N alice
STATS O helper
```

`STATS N <account>` reports NickServ account state, including `created=`, `updated=`, and `last_identified=` as UTC text. `STATS O <oper>` reports operator account state, including `created=`, `updated=`, and `last_opered=` as UTC text. A timestamp that has never been set is reported as `never`.

Network administrators can also see the same data through the existing administration commands:

```text
NSINFO alice
OPERLIST helper
```

`NSINFO` includes `last_identified=`. `OPERLIST` includes `last_opered=`. These commands format timestamps as UTC text and split long responses safely across NOTICE lines when needed.

## Security notes

These timestamps are successful-authentication audit metadata only. They do not record failed password attempts, client IP addresses, hostnames, or password material. For live client identity, use existing operator tools such as `WHOIS` and `USERIP` while the client is connected.

Keep `nickserv.db`, `operators.db`, and their backups protected as secrets because they still contain password hashes and other administrative state.
