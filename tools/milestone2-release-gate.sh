#!/usr/bin/env bash
# Milestone 2 release-gate runner for ScratchIRCd.
#
# This script is intentionally explicit and evidence-oriented. It creates
# separate build directories, captures toolchain/dependency details, runs the
# full regression suite, runs strict GCC/Clang Release builds when available,
# runs a focused sanitizer pass, and runs the operational soak smoke/release
# gates. It does not install ScratchIRCd or modify live service state.

set -euo pipefail

ROOT=${SCRATCHIRCD_SOURCE_DIR:-$(pwd)}
OUT=${SCRATCHIRCD_RELEASE_EVIDENCE_DIR:-"$ROOT/release-evidence/milestone-2"}
JOBS=${SCRATCHIRCD_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')}
SANITIZER_CTEST_REGEX=${SCRATCHIRCD_SANITIZER_CTEST_REGEX:-'^(chanserv_restore_fail_closed_integration|chanserv_database|chanserv_persistence|chanserv_integration|chanserv_policy_controls_integration|chanserv_founder_transfer_integration|chanserv_identity_transitions_integration|chanserv_successor_promotion_integration|chanserv_persistence_integration|nickserv_integration|sasl_integration|ircv3_cap_integration|history_integration|operator_actions_integration)$'}

mkdir -p "$OUT"

log() {
    printf '\n== %s ==\n' "$*"
}

run_logged() {
    local name=$1
    shift
    log "$name"
    printf '$'
    printf ' %q' "$@"
    printf '\n'
    "$@" 2>&1 | tee "$OUT/$name.log"
    local rc=${PIPESTATUS[0]}
    printf '%s: exit %d\n' "$name" "$rc" | tee -a "$OUT/summary.log"
    return "$rc"
}

capture_text() {
    local file=$1
    shift
    {
        printf '$'
        printf ' %q' "$@"
        printf '\n'
        "$@" 2>&1 || true
    } > "$OUT/$file"
}

cd "$ROOT"
: > "$OUT/summary.log"

log "Milestone 2 release evidence"
{
    printf 'source_dir=%s\n' "$ROOT"
    printf 'evidence_dir=%s\n' "$OUT"
    printf 'jobs=%s\n' "$JOBS"
    printf 'commit=%s\n' "$(git rev-parse HEAD 2>/dev/null || printf unknown)"
    printf 'branch=%s\n' "$(git rev-parse --abbrev-ref HEAD 2>/dev/null || printf unknown)"
    printf 'date_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'kernel=%s\n' "$(uname -a)"
} | tee "$OUT/environment.txt"

capture_text cmake-version.txt cmake --version
capture_text gcc-version.txt gcc --version
if command -v clang >/dev/null 2>&1; then
    capture_text clang-version.txt clang --version
else
    printf 'clang not found; strict Clang gate not run.\n' | tee "$OUT/clang-version.txt"
fi
capture_text sqlite3-version.txt sh -c 'sqlite3 --version 2>/dev/null || pkg-config --modversion sqlite3 2>/dev/null || true'
capture_text openssl-version.txt openssl version -a
capture_text pkg-config-deps.txt sh -c 'pkg-config --modversion sqlite3 openssl 2>/dev/null || true'

run_logged full-gcc-config cmake -S . -B build-m2-gcc -DCMAKE_BUILD_TYPE=Release -DSCRATCHIRCD_WARNINGS_AS_ERRORS=ON -DSCRATCHIRCD_ENABLE_SANITIZERS=OFF
run_logged full-gcc-build cmake --build build-m2-gcc --target scratchircd scratchircd-mkpasswd -j"$JOBS"
run_logged full-gcc-ctest ctest --test-dir build-m2-gcc --output-on-failure

if command -v clang >/dev/null 2>&1; then
    run_logged full-clang-config env CC=clang cmake -S . -B build-m2-clang -DCMAKE_BUILD_TYPE=Release -DSCRATCHIRCD_WARNINGS_AS_ERRORS=ON -DSCRATCHIRCD_ENABLE_SANITIZERS=OFF
    run_logged full-clang-build cmake --build build-m2-clang --target scratchircd scratchircd-mkpasswd -j"$JOBS"
    run_logged full-clang-ctest ctest --test-dir build-m2-clang --output-on-failure
fi

run_logged sanitizer-config cmake -S . -B build-m2-sanitize -DCMAKE_BUILD_TYPE=Debug -DSCRATCHIRCD_WARNINGS_AS_ERRORS=ON -DSCRATCHIRCD_ENABLE_SANITIZERS=ON
run_logged sanitizer-build cmake --build build-m2-sanitize --target scratchircd scratchircd-mkpasswd -j"$JOBS"
run_logged sanitizer-ctest env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build-m2-sanitize -R "$SANITIZER_CTEST_REGEX" --output-on-failure

run_logged soak-smoke python3 tools/run_soak.py ./build-m2-gcc/scratchircd --duration-seconds 30 --clients 6 --cycle-delay-seconds 0.1 --sample-interval-seconds 5
run_logged soak-release-gate python3 tests/integration/test_soak_release_gate.py tools/run_soak.py ./build-m2-gcc/scratchircd

log "Evidence written"
printf 'Milestone 2 evidence directory: %s\n' "$OUT"
printf 'Review %s/summary.log and attach/copy the relevant logs before tagging.\n' "$OUT"
