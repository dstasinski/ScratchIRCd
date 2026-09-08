# Milestone 2 Release Notes

ScratchIRCd Milestone 2 qualifies the account, channel-service, and modern IRC-client surfaces on the `Genesis` branch. This milestone builds on the bounded single-server core from Milestone 1 and focuses on supported use of NickServ, ChanServ, SASL, IRCv3 capability negotiation, persistent channel state, and service lifecycle behavior.

## Release status

Status: release-ready.

The Milestone 2 feature and behavior work is complete. A local release-gate run has been reported as successful, including the focused sanitizer CTest subset, soak smoke, and soak release gate. A longer 12-hour release-candidate soak also passed against commit `fe6b21d`.

Long-soak summary:

```text
soak evidence: /home/daniel/Projects/ScratchIRCd/soak-fe6b21d.json
soak passed: elapsed=43203.567s churn=35005 rss_growth=2224KiB fd_growth=3
```

Before creating the final tag, retain the generated `release-evidence/milestone-2/` directory and the `soak-fe6b21d.json` report outside the Git checkout as release evidence.

## Major additions and behavior qualified

- NickServ account registration, identification, password changes, recovery, email verification and reset, account persistence, and protections against unauthorized account access.
- IRCv3 SASL PLAIN authentication with malformed-input handling and account state consistent with NickServ identification.
- Automatic user mode `+x` after successful registration, with cloaked display-host behavior kept separate from real IP and real hostname identity.
- WHOIS idle time separated from PING liveness. Messaging idle changes only after delivered `PRIVMSG`; PING/PONG timeout handling remains independent.
- Advertised and enforced nickname, username, and channel-name limits of 15, 10, and 32 characters.
- IRCv3 CAP negotiation for account-notify, away-notify, extended JOIN, labeled-response, message-tags, SASL, server-time, and channel history behavior.
- Persistent channel history and modern-client integration coverage.
- Network-operator and administrator `FLASH <#channel|nick[,nick...]|*> :<message>` delivered through server numeric `343`.

## ChanServ and registered-channel work

Milestone 2 substantially hardens ChanServ while preserving the architectural rule that ChanServ is virtual, acts through server authority, and never joins channels.

- Persistent ChanServ registration with founder, access roles, MLOCK, TOPIC, TOPICLOCK, SECUREOPS, SUCCESSOR, GREETING, and runtime channel state.
- `CHANSERV REGISTER` and `CHANSERV DROP` remain network-admin-only through both direct `/CHANSERV` command use and `PRIVMSG ChanServ` routing.
- Live `+r` and `-r` reconciliation for `REGISTER`, `DROP`, `CSDROP`, `CSSET ENABLED`, founder disable/drop, and re-enable paths.
- Founder lifecycle handling promotes valid successors, disables orphaned registrations, rejects invalid re-enable attempts, and preserves or removes successor/access references according to NickServ disable/drop semantics.
- SecureOps reconciliation prevents unauthorized protected privileges and removes invalid manual operator-style privileges while leaving voice unrestricted.
- Channel recreation has been audited. When a registered channel becomes empty and is later recreated, ChanServ registration, policy, topic, runtime parameter modes, and masks are restored before JOIN admission succeeds.
- Registered-channel runtime restore failures now fail closed on JOIN and do not leave an empty live channel behind.
- The ChanServ restore-fail-closed integration test is registered in CTest.

## Operations and release tooling

- `tools/scratchircd-start.sh` provides a standalone Linux/Bash launcher template for deployments that are not using systemd. It supports `start`, `stop`, `restart`, and `status`, keeps state under a private state directory, and avoids Git, builds, installation, downloads, and root privileges.
- The README includes brief launcher guidance, while the Network Administrator Guide contains the detailed standalone-launcher procedure and cautions about not combining it with systemd, another supervisor, or the update-and-restart script.
- `tools/milestone2-release-gate.sh` captures release evidence for GCC strict Release builds, Clang strict Release builds when available, full CTest runs, focused sanitizer tests, toolchain/dependency versions, soak smoke, and soak release-gate validation.
- `.gitignore` excludes generated release evidence, soak reports, and Python bytecode/cache files so release-candidate soaks can enforce a clean checkout.

## Validation evidence to preserve before tagging

Before the final Milestone 2 tag, save or copy the relevant contents of:

```sh
release-evidence/milestone-2/
soak-fe6b21d.json
```

At minimum, preserve:

```text
environment.txt
summary.log
full-gcc-config.log
full-gcc-build.log
full-gcc-ctest.log
full-clang-config.log, if Clang was available
full-clang-build.log, if Clang was available
full-clang-ctest.log, if Clang was available
sanitizer-config.log
sanitizer-build.log
sanitizer-ctest.log
soak-smoke.log
soak-release-gate.log
soak-fe6b21d.json
```

The final tag should identify the exact tested commit, compiler versions, dependency versions, CTest output, sanitizer output, and soak evidence.

## Final pre-tag checklist

- Confirm the final `Genesis` checkout matches the intended tested commit.
- Confirm `summary.log` reports successful exits for the required build, test, sanitizer, and soak gates.
- Preserve `soak-fe6b21d.json` as the 12-hour soak record.
- Review README, Network Administrator Guide, Client Guide, Operator Guide, NickServ Guide, ChanServ Guide, IRCv3 Guide, and storage/security documentation for obvious stale references.
- Create the Milestone 2 tag only after the evidence and release notes are accepted.

## Suggested tag name

```text
milestone-2
```

If you prefer semantic versioning instead of milestone tags, tag the exact tested commit with the next chosen project version and keep this file as the milestone release note body.
