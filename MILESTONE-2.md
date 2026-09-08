# Milestone 2: Accounts, Channel Services, and Modern IRC Clients

Milestone 2 qualifies ScratchIRCd's account, channel-service, and IRCv3 surfaces for supported use on the `Genesis` branch. It builds on the bounded secure core established by Milestone 1.

## Scope

- NickServ registration, identification, password changes, account persistence, recovery, and protections against unauthorized account access.
- SASL PLAIN authentication with safe malformed-input handling and account state consistent with NickServ identification.
- Persistent ChanServ registration, founder and access permissions, mode locks, topics, runtime channel state, founder lifecycle handling, and service-controlled visibility. ChanServ remains virtual and never joins channels.
- IRCv3 CAP negotiation, account-notify, away-notify, extended JOIN, message tags, labeled responses, server time, and persistent channel history exercised with modern clients.

MemoServ expansion, persistent direct-message history, and additional external integrations remain outside this milestone unless promoted separately.

## Completed milestone work

- Automatic user mode `+x` after successful registration.
- WHOIS idle/liveness separation so delivered `PRIVMSG` activity and PING timeout handling use distinct clocks.
- Enforced and advertised nickname, username, and channel-name limits of 15, 10, and 32 characters, with regression and soak coverage.
- Operator and network-administrator `FLASH <#channel|nick[,nick...]|*> :<message>` delivered as server numeric `343`.
- README trimmed to introductory, feature, and installation material; detailed behavior lives under `docs/`.
- Persistent ChanServ MLOCK, TOPIC, TOPICLOCK, SECUREOPS, SUCCESSOR, GREETING, access levels, and runtime channel state.
- ChanServ `REGISTER`, `DROP`, `CSDROP`, `CSSET ENABLED`, founder disable/drop, and re-enable paths reconcile live `+r`/`-r` state visibly.
- Founder lifecycle handling promotes valid successors, disables orphaned registrations, prevents invalid re-enable, and preserves or removes account references according to NickServ disable/drop semantics.
- Channel recreation audit completed. Recreated registered channels restore ChanServ registration, policy, topic, runtime parameter modes, and masks before JOIN admission succeeds.
- Registered-channel runtime restore failures fail closed on JOIN, and the failed JOIN does not leave an empty live channel behind.
- The ChanServ restore-fail-closed integration test is registered in CTest.
- A standalone Linux launcher template is available at `tools/scratchircd-start.sh` and documented in the README and Network Administrator Guide.
- A reproducible release-gate runner is available at `tools/milestone2-release-gate.sh` to capture build, test, toolchain, dependency, sanitizer, and soak evidence, and exits nonzero on any required gate failure.
- A local Milestone 2 release-gate run reached completion with the focused sanitizer CTest subset passing 14/14 tests, the soak smoke passing, and the soak release gate passing.

## Remaining release gates

- Review and retain the generated `release-evidence/milestone-2/` logs for the final tested commit.
- Record the exact tested commit, compiler versions, dependency versions, test command output, and soak evidence before tagging.
- Decide whether to run a longer manual soak beyond the built-in release-gate smoke before tagging.

## Completion estimate

Feature and behavior work for Milestone 2 is effectively complete. The release-gate runner has completed successfully in local validation. Milestone 2 is approximately 95 to 97 percent complete; the remaining work is final evidence review and tag preparation.

## Completion gate

- GCC and Clang strict Release builds complete without warnings where the compilers are available.
- The complete regression suite and focused sanitizer runs pass.
- NickServ accounts and ChanServ settings survive daemon restart.
- Permission tests prevent account and registered-channel takeover.
- Channel recreation and ChanServ runtime restore failures behave fail-closed.
- The operational soak exercises account, channel, modern-client, naming-limit, and lifecycle paths without Milestone 1 regressions.
- The exact tested commit, toolchain, dependencies, and soak evidence are recorded before tagging.
