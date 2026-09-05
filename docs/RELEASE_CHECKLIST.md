# Milestone 2 Release Checklist

Use this checklist on the exact `Genesis` commit that will be tagged. A passing
CI run is necessary, but does not replace the local operational soak.

## 1. Freeze the candidate

Start from a clean tree and record the candidate commit:

```sh
git switch Genesis
git pull --ff-only origin Genesis
git status --short
git rev-parse HEAD
```

Do not tag if the worktree is dirty or the tested commit differs from the
candidate commit.

## 2. Strict release build

```sh
CC=gcc cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DSCRATCHIRCD_WARNINGS_AS_ERRORS=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

Confirm the GitHub Actions run for the same commit passes GCC Debug, GCC
Release, Clang Debug, and Clang Debug with AddressSanitizer and
UndefinedBehaviorSanitizer.

## 3. Local sanitizer pass

```sh
CC=clang cmake -S . -B build-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSCRATCHIRCD_WARNINGS_AS_ERRORS=ON \
  -DSCRATCHIRCD_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-sanitize --output-on-failure
```

## 4. Operational soak

Run the release binary for at least 12 hours; use 24 hours for the final
candidate when practical:

```sh
python3 tools/run_soak.py build-release/scratchircd \
  --duration-hours 12 \
  --clients 12 \
  --release-candidate \
  --report "soak-$(git rev-parse --short HEAD).json"
```

The runner alternates clean and abrupt transient disconnects while stable
IRCv3 clients exchange channel and private messages. Its preflight validates
the configured nickname, username, and channel-name limits. It then exercises
NickServ registration, SASL PLAIN, account-notify, ChanServ registration,
persistent MLOCK and topic policy, away-notify, extended-join, message-tags,
labeled-response, server-time, and persistent CHATHISTORY. The Milestone 1
no-spoof, CTCP VERSION, PING/PONG, channel-membership, churn, and graceful
shutdown paths remain active.

By default the runner permits at most 32 MiB of RSS growth and 16 additional
file descriptors above the post-provisioning baseline. Tighten those limits
when the deployment baseline is known; do not raise them merely to make a
failed run pass.

The command must exit zero and the JSON record must contain both
`"passed": true`, `"release_qualified": true`, and a nonzero value for every
entry in `milestone2_coverage`. Release-candidate mode fails
before starting the daemon when the requested duration is under 12 hours, the
checkout is dirty, the build is not `Release`, or warnings-as-errors was not
enabled. Investigate every daemon exit, stable-client disconnect, protocol
stall, sanitizer diagnostic, or resource-limit failure before restarting the
soak.

## 5. Preserve release evidence

Archive together:

- the full candidate commit SHA and GitHub Actions run URL;
- strict and sanitizer build/test logs;
- the soak JSON record;
- the production configuration with secrets removed;
- the target Linux distribution, compiler, CMake, OpenSSL, SQLite, Argon2, and
  MaxMind DB library versions;
- the intended installation prefix and restart procedure.

The soak record captures the binary SHA-256, linked libraries, compiler,
CMake, OpenSSL, generated test configuration, traffic totals, Milestone 2
coverage totals, resource samples, and shutdown result. Compare its
`git_commit` with the candidate commit and its binary digest with the binary
being packaged.

## 6. Tag only after every gate passes

Reconfirm the tree and remote CI state, then create the release tag from the
tested commit. Any source, build-option, or dependency change invalidates the
previous build, sanitizer, and soak evidence.
