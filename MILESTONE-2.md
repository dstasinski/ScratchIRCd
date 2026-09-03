# Milestone 2: Accounts, Channel Services, and Modern IRC Clients

Milestone 2 qualifies ScratchIRCd's account, channel-service, and IRCv3 surfaces for supported use on the `Genesis` branch. It builds on the bounded secure core established by Milestone 1.

## Scope

- NickServ registration, identification, password changes, account persistence, recovery, and protections against unauthorized account access.
- SASL PLAIN authentication with safe malformed-input handling and account state consistent with NickServ identification.
- Persistent ChanServ registration, founder and access permissions, mode locks, and topics. ChanServ remains virtual and never joins channels.
- IRCv3 CAP negotiation, account-notify, away-notify, extended JOIN, message tags, labeled responses, server time, and persistent channel history exercised with modern clients.

MemoServ expansion, persistent direct-message history, and additional external integrations remain outside this milestone unless promoted separately.

## Priority fixes

- Grant user mode `+x` automatically when a client completes registration.
- Reset WHOIS idle time only after a successfully delivered private or channel `PRIVMSG`; maintain PING liveness on an independent clock.
- Enforce and advertise nickname, username, and channel-name limits of 15, 10, and 32 characters respectively, with matching regression and soak coverage.
- Provide IRC operators and network administrators with `FLASH <#channel|nick[,nick...]|*> :<message>`, delivered as server numeric `343`.
- Keep the project README limited to an introduction, feature list, and installation instructions; detailed behavior belongs in `docs/` and milestone files.

## Completion gate

- GCC and Clang strict Release builds complete without warnings.
- The complete regression suite and focused sanitizer runs pass.
- NickServ accounts and ChanServ settings survive daemon restart.
- Permission tests prevent account and registered-channel takeover.
- The operational soak exercises account, channel, modern-client, naming-limit, and lifecycle paths without Milestone 1 regressions.
- The exact tested commit, toolchain, dependencies, and soak evidence are recorded before tagging.
