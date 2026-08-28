#!/usr/bin/env bash
set -Eeuo pipefail

readonly SCRIPT_NAME=${0##*/}
readonly REQUIRED_BRANCH=Genesis
readonly BUILD_DIR_NAME=${BUILD_DIR_NAME:-build}
readonly BUILD_TYPE=${BUILD_TYPE:-Release}
readonly INSTALL_PREFIX=${INSTALL_PREFIX:-/usr/local}
readonly STOP_TIMEOUT_SECONDS=${STOP_TIMEOUT_SECONDS:-15}
readonly STARTUP_CHECK_SECONDS=${STARTUP_CHECK_SECONDS:-2}
readonly LOG_FILE=${SCRATCHIRCD_LOG_FILE:-scratchircd.log}

die() {
    printf '%s: %s\n' "$SCRIPT_NAME" "$*" >&2
    exit 1
}

run_install() {
    if [[ $(id -u) -eq 0 ]]; then
        cmake --install "$build_dir" --prefix "$INSTALL_PREFIX"
    elif command -v sudo >/dev/null 2>&1; then
        sudo cmake --install "$build_dir" --prefix "$INSTALL_PREFIX"
    else
        die "installation to $INSTALL_PREFIX requires root privileges or sudo"
    fi
}

[[ ${1:-} != --help ]] || {
    cat <<'EOF'
Usage: ./update-and-restart.sh

Environment overrides:
  BUILD_DIR_NAME          CMake build directory (default: build)
  BUILD_TYPE              CMake build type (default: Release)
  INSTALL_PREFIX          Install prefix (default: /usr/local)
  STOP_TIMEOUT_SECONDS    Graceful-stop timeout (default: 15)
  STARTUP_CHECK_SECONDS   Initial restart check (default: 2)
  SCRATCHIRCD_LOG_FILE    Restart output log, relative to the old process cwd
EOF
    exit 0
}

command -v git >/dev/null 2>&1 || die "git is required"
command -v cmake >/dev/null 2>&1 || die "cmake is required"
command -v pgrep >/dev/null 2>&1 || die "pgrep is required"

repo_dir=$(git rev-parse --show-toplevel 2>/dev/null) ||
    die "run this script from inside the ScratchIRCd repository"
cd "$repo_dir"

branch=$(git branch --show-current)
[[ $branch == "$REQUIRED_BRANCH" ]] ||
    die "current branch is '$branch'; expected '$REQUIRED_BRANCH'"

[[ -z $(git status --porcelain --untracked-files=no) ]] ||
    die "tracked files have local changes; commit or stash them before updating"

git fetch origin "$REQUIRED_BRANCH"
git merge --ff-only "origin/$REQUIRED_BRANCH"

# Re-exec the copy just pulled so future updates to this script take effect in
# the same run without continuing from a stale, partially-read shell file.
if [[ ${1:-} != --after-pull ]]; then
    exec "$repo_dir/$SCRIPT_NAME" --after-pull
fi

mapfile -t daemon_pids < <(pgrep -x scratchircd || true)
[[ ${#daemon_pids[@]} -le 1 ]] ||
    die "multiple scratchircd processes are running; stop/select them manually"

daemon_pid=
daemon_cwd=
declare -a daemon_args=()
if [[ ${#daemon_pids[@]} -eq 1 ]]; then
    daemon_pid=${daemon_pids[0]}
    [[ -r /proc/$daemon_pid/cmdline ]] ||
        die "cannot read command line for running PID $daemon_pid"
    daemon_cwd=$(readlink -f "/proc/$daemon_pid/cwd") ||
        die "cannot resolve working directory for PID $daemon_pid"
    mapfile -d '' -t daemon_args < "/proc/$daemon_pid/cmdline"
    [[ ${#daemon_args[@]} -ge 1 ]] ||
        die "running PID $daemon_pid has an empty command line"
fi

build_dir="$repo_dir/$BUILD_DIR_NAME"
cmake -S "$repo_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DSCRATCHIRCD_WARNINGS_AS_ERRORS=ON
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure
run_install

installed_binary="$INSTALL_PREFIX/bin/scratchircd"
[[ -x $installed_binary ]] ||
    die "installed daemon was not found at $installed_binary"

if [[ -z $daemon_pid ]]; then
    printf 'Build, tests, and installation succeeded.\n'
    printf 'No running scratchircd process was found; start it manually with a configuration file.\n'
    exit 0
fi

kill -TERM "$daemon_pid"
deadline=$((SECONDS + STOP_TIMEOUT_SECONDS))
while kill -0 "$daemon_pid" 2>/dev/null; do
    (( SECONDS < deadline )) ||
        die "PID $daemon_pid did not stop within $STOP_TIMEOUT_SECONDS seconds; it was not force-killed"
    sleep 1
done

# Preserve every original argument except argv[0], which must name the newly
# installed binary. Relative configuration paths retain the old process cwd.
daemon_args[0]=$installed_binary
log_path=$LOG_FILE
[[ $log_path == /* ]] || log_path="$daemon_cwd/$log_path"

(
    cd "$daemon_cwd"
    nohup "${daemon_args[@]}" >>"$log_path" 2>&1 &
    printf '%s\n' $! >"$repo_dir/.scratchircd-restart.pid"
)

new_pid=$(<"$repo_dir/.scratchircd-restart.pid")
rm -f "$repo_dir/.scratchircd-restart.pid"
sleep "$STARTUP_CHECK_SECONDS"
kill -0 "$new_pid" 2>/dev/null ||
    die "new daemon exited during startup; inspect $log_path"

printf 'ScratchIRCd updated and restarted successfully (PID %s).\n' "$new_pid"
printf 'Executable: %s\n' "$installed_binary"
printf 'Log: %s\n' "$log_path"
