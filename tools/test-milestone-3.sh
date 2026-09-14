#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

usage() {
    cat <<EOF
usage: $0 [build-dir] [focused|broad|all]

Runs the Milestone 3 local verification gates from the repository root.

Arguments:
  build-dir        CMake build directory to use. Default: build-m3-gcc
  focused          Build and run the focused DB/schema closeout tests. Default.
  broad            Build daemon tools and run the broader Milestone 3 integration tests.
  all              Run focused first, then broad.
EOF
}

case "${1:-}" in
  -h|--help)
    usage
    exit 0
    ;;
esac

build_dir=${1:-build-m3-gcc}
mode=${2:-focused}

case "$mode" in
  focused|broad|all) ;;
  *)
    usage >&2
    exit 2
    ;;
esac

cd "$repo_root"

jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

cmake -S . -B "$build_dir"

run_focused() {
    cmake --build "$build_dir" \
      --target test_current_schema_only test_chanserv_logging_db \
               test_geoban_db test_history_db test_ban_db \
               test_chanserv_db test_memoserv_db test_nickserv_db test_operator_db \
      -j"$jobs"

    ctest --test-dir "$build_dir" \
      -R 'current_schema_only|chanserv_logging_database|geoban_database|history_database|ban_database|chanserv_database|memoserv_database|nickserv_database|operator_database' \
      --output-on-failure
}

run_broad() {
    cmake --build "$build_dir" --target scratchircd scratchircd-mkpasswd -j"$jobs"

    ctest --test-dir "$build_dir" \
      -R 'memoserv|eline|reserved_nick|nickserv|operator|oper|geoban|history|chanserv' \
      --output-on-failure
}

case "$mode" in
  focused) run_focused ;;
  broad) run_broad ;;
  all)
    run_focused
    run_broad
    ;;
esac
