#!/usr/bin/env bash
# ScratchIRCd standalone launcher for Linux / Bash 4.2+.
# This checked-in copy is a deployment template. Copy the installed launcher
# outside the Git checkout, e.g. ~/scratchircd-start.sh, before using it.
# Run as the same unprivileged account that owns your IRC configuration/data.
# No downloads, builds, installation, Git operations, or root privileges.
#
# Examples:
#   chmod +x ~/scratchircd-start.sh
#   ~/scratchircd-start.sh start
#   ~/scratchircd-start.sh status
#   ~/scratchircd-start.sh stop
#   ~/scratchircd-start.sh restart
#
# EDIT THESE DEFAULTS if your installation differs. Environment overrides also
# work, but use the same SCRATCHIRCD_STATE_DIR for all control commands.
WORK_DIR=${SCRATCHIRCD_HOME:-"$HOME/ScratchIRCd"}
BINARY=${SCRATCHIRCD_BIN:-/usr/local/bin/scratchircd}
CONFIG=${SCRATCHIRCD_CONFIG:-ircd.conf}
STATE_DIR=${SCRATCHIRCD_STATE_DIR:-"${XDG_STATE_HOME:-$HOME/.local/state}/scratchircd"}
RESTART_DELAY=${SCRATCHIRCD_RESTART_DELAY:-5}
STOP_TIMEOUT=${SCRATCHIRCD_STOP_TIMEOUT:-30}

set -u -o pipefail
umask 077

fail() { printf 'scratchircd-start: %s\n' "$*" >&2; return 1; }

usage() {
    printf '%s\n' 'Usage: scratchircd-start.sh {start|stop|restart|status|help}' \
        '' \
        'start    Start a detached supervisor; SSH logout does not stop it.' \
        'stop     Stop supervision and send SIGTERM to its daemon; never SIGKILL.' \
        'restart  Stop cleanly, then start with the current settings/binary.' \
        'status   Show supervisor/daemon PIDs and the console log location.' \
        '' \
        'Defaults: ~/ScratchIRCd, /usr/local/bin/scratchircd, ircd.conf.' \
        'Relative configuration and binary paths are relative to WORK_DIR.' \
        'Database/MOTD/certificate paths retain WORK_DIR as their working directory.' \
        '' \
        'The daemon restarts after ANY exit, including /DIE and a direct SIGTERM.' \
        'Use this script stop for a lasting shutdown. /RESTART works normally.' \
        'Restarting after a crash drops existing clients; it is not seamless.' \
        'This is process-exit supervision, not a hang detector or a health check.' \
        '' \
        'Stop a manually started daemon before the first start. This launcher' \
        'refuses to start alongside another same-user process named scratchircd.' \
        'It does not adopt or kill an unrelated/manually started daemon.' \
        'Do not combine it with a second supervisor or the old update-and-restart' \
        'script while running: stop here, run the updater, then start here.' \
        '' \
        'Settings can be edited above or overridden with:' \
        'SCRATCHIRCD_HOME, SCRATCHIRCD_BIN, SCRATCHIRCD_CONFIG,' \
        'SCRATCHIRCD_STATE_DIR, SCRATCHIRCD_RESTART_DELAY, SCRATCHIRCD_STOP_TIMEOUT.' \
        'The last two settings are whole seconds, from 1 to 3600.' \
        '' \
        'Logs/state default to ~/.local/state/scratchircd (private to your user).' \
        'console.log appends daemon output and supervisor events; rotate/archive' \
        'it periodically. For a simple safe rotation, stop, move the log, start.' \
        '' \
        'Optional start after a machine reboot: add this to the ircd user crontab' \
        '(crontab -e), adjusting /home/ircd if necessary:' \
        '@reboot /home/ircd/scratchircd-start.sh start' \
        'This script does not install a cron entry or enable boot startup itself.'
}

ACTION=${1:-start}
[[ $# -le 1 ]] || { usage >&2; exit 2; }
case "$ACTION" in
    help|-h|--help) usage; exit 0 ;;
    start|stop|restart|status|__supervise) ;;
    *) usage >&2; exit 2 ;;
esac

for tool in flock realpath readlink stat pgrep nohup bash sleep mv rm mkdir; do
    command -v "$tool" >/dev/null 2>&1 || { fail "Required command missing: $tool"; exit 1; }
done
for value in "$RESTART_DELAY" "$STOP_TIMEOUT"; do
    [[ $value =~ ^[1-9][0-9]{0,3}$ ]] && (( value <= 3600 )) || {
        fail 'Restart delay and stop timeout must be integers from 1 to 3600.'; exit 2;
    }
done

SELF=$(realpath -e -- "${BASH_SOURCE[0]}") || exit 1
WORK_DIR=$(realpath -m -- "$WORK_DIR") || exit 1
[[ $BINARY == /* ]] || BINARY="$WORK_DIR/$BINARY"
[[ $CONFIG == /* ]] || CONFIG="$WORK_DIR/$CONFIG"
[[ $STATE_DIR == /* ]] || { fail 'STATE_DIR must be an absolute path.'; exit 2; }
[[ ! -L $STATE_DIR ]] || { fail 'STATE_DIR must not be a symbolic link.'; exit 1; }
mkdir -p -m 700 -- "$STATE_DIR" || exit 1
[[ -d $STATE_DIR && -O $STATE_DIR ]] || { fail 'STATE_DIR must be owned by this user.'; exit 1; }
permissions=$(stat -c '%a' -- "$STATE_DIR") || exit 1
(( (8#$permissions & 077) == 0 )) || {
    fail "STATE_DIR must be private (chmod 700): $STATE_DIR"; exit 1;
}
for name in control.lock supervisor.lock supervisor.pid daemon.pid stop.requested console.log; do
    path="$STATE_DIR/$name"
    [[ ! -L $path && ( ! -e $path || -f $path ) ]] || {
        fail "Unsafe state/log file: $path"; exit 1;
    }
done

LOCK="$STATE_DIR/supervisor.lock"
SUPERVISOR="$STATE_DIR/supervisor.pid"
DAEMON="$STATE_DIR/daemon.pid"
STOP_FILE="$STATE_DIR/stop.requested"
LOG="$STATE_DIR/console.log"
IFS= read -r BOOT_ID < /proc/sys/kernel/random/boot_id || {
    fail 'Linux /proc is required.'; exit 1;
}

# Normally /proc PIDs and shell PIDs match. Some container environments expose
# their parent's procfs instead. Resolve only same-namespace, same-user PIDs in
# that case; never send a signal using a parent-namespace PID.
IFS= read -r self_stat < /proc/self/stat || exit 1
PROC_NATIVE=1
[[ ${self_stat%% *} == "$$" ]] || PROC_NATIVE=0
PID_NAMESPACE=$(readlink /proc/self/ns/pid) || exit 1

status_pid() {
    local file=$1 key value
    STATUS_PID=
    while read -r key value; do
        if [[ $key == NSpid: ]]; then
            STATUS_PID=${value##*[[:space:]]}
            [[ $STATUS_PID =~ ^[1-9][0-9]*$ ]]
            return $?
        fi
    done 2>/dev/null < "$file"
    return 1
}

resolve_proc_pid() {
    local pid=$1 entry host
    if (( PROC_NATIVE )); then PROC_HOST_PID=$pid; return 0; fi
    for entry in /proc/[0-9]*/status; do
        [[ -O $entry ]] || continue
        status_pid "$entry" && [[ $STATUS_PID == "$pid" ]] || continue
        host=${entry%/status}; host=${host##*/}
        [[ $(readlink "/proc/$host/ns/pid" 2>/dev/null) == "$PID_NAMESPACE" ]] || continue
        PROC_HOST_PID=$host
        return 0
    done
    return 1
}

# A PID alone is unsafe: also check its boot and Linux process-start timestamp.
# Never source a PID/state file as shell code.
proc_info() {
    local pid=$1 line
    local -a fields=()
    [[ $pid =~ ^[1-9][0-9]*$ ]] || return 1
    resolve_proc_pid "$pid" || return 1
    [[ -O /proc/$PROC_HOST_PID ]] || return 1
    IFS= read -r line 2>/dev/null < "/proc/$PROC_HOST_PID/stat" || return 1
    read -r -a fields <<< "${line##*) }"
    [[ ${#fields[@]} -ge 20 ]] || return 1
    PROC_STATE=${fields[0]}
    PROC_PARENT=${fields[1]}
    PROC_START=${fields[19]}
    if (( ! PROC_NATIVE )); then
        if status_pid "/proc/$PROC_PARENT/status"; then
            PROC_PARENT=$STATUS_PID
        else
            PROC_PARENT=0
        fi
    fi
    return 0
}

record_pid() {
    local file=$1 pid=$2 expected_parent=${3:-} tmp
    proc_info "$pid" || return 1
    [[ -z $expected_parent || $PROC_PARENT == "$expected_parent" ]] || return 1
    tmp="$file.tmp.$$"
    printf '%s %s %s\n' "$pid" "$PROC_START" "$BOOT_ID" > "$tmp" || return 1
    mv -f -- "$tmp" "$file"
}

pid_alive() {
    local file=$1 pid birth boot extra
    [[ -f $file ]] || return 1
    read -r pid birth boot extra < "$file" || return 1
    [[ -z ${extra:-} && $birth =~ ^[0-9]+$ && $boot == "$BOOT_ID" ]] || return 1
    proc_info "$pid" || return 1
    [[ $PROC_START == "$birth" && $PROC_STATE != Z && $PROC_STATE != X ]] || return 1
    LIVE_PID=$pid
}

signal_recorded() {
    if pid_alive "$1"; then
        kill -TERM "$LIVE_PID" 2>/dev/null || true
    fi
}

lock_held() { ! flock -n "$LOCK" true; }
log_event() { printf '[%(%Y-%m-%dT%H:%M:%S%z)T] supervisor: %s\n' -1 "$*"; }

other_daemon() {
    local result entry host name pid
    if (( ! PROC_NATIVE )); then
        for entry in /proc/[0-9]*/comm; do
            [[ -O $entry ]] || continue
            IFS= read -r name 2>/dev/null < "$entry" || continue
            [[ $name == scratchircd ]] || continue
            host=${entry%/comm}; host=${host##*/}
            [[ $(readlink "/proc/$host/ns/pid" 2>/dev/null) == "$PID_NAMESPACE" ]] || continue
            status_pid "/proc/$host/status" || continue
            OTHER_PIDS=$STATUS_PID
            return 0
        done
        return 1
    fi

    result=$(pgrep -u "$EUID" -x scratchircd 2>/dev/null)
    case $? in
        0)
            OTHER_PIDS=
            for pid in $result; do
                if proc_info "$pid" && [[ $PROC_STATE != Z && $PROC_STATE != X ]]; then
                    OTHER_PIDS+="${OTHER_PIDS:+,}$pid"
                fi
            done
            [[ -n $OTHER_PIDS ]]
            return $?
            ;;
        1) return 1 ;;
        *) OTHER_PIDS='unknown (process check failed)'; return 0 ;;
    esac
}

preflight() {
    [[ -d $WORK_DIR && -x $WORK_DIR ]] || { fail "Working directory unavailable: $WORK_DIR"; return 1; }
    [[ -f $BINARY && -x $BINARY ]] || { fail "Executable unavailable: $BINARY (edit BINARY above)"; return 1; }
    [[ -f $CONFIG && -r $CONFIG ]] || { fail "Configuration unreadable: $CONFIG (edit CONFIG above)"; return 1; }
}

supervise() {
    # FD 9 is deliberately inherited by the daemon: even if this supervisor is
    # killed abruptly, an orphaned daemon keeps the lock until it has stopped.
    exec 9>"$LOCK" || return 1
    flock -n 9 || { fail 'Another supervisor or its daemon still holds the lock.'; return 1; }
    preflight || return 1
    cd -- "$WORK_DIR" || return 1
    rm -f -- "$STOP_FILE" "$DAEMON" || return 1
    stopping=0
    child=
    trap 'stopping=1; signal_recorded "$DAEMON"' TERM INT
    trap '' HUP
    trap cleanup_supervisor EXIT
    record_pid "$SUPERVISOR" "$$" || return 1
    log_event "Started; binary=$BINARY; config=$CONFIG; cwd=$WORK_DIR"

    while (( ! stopping )) && [[ ! -e $STOP_FILE ]]; do
        if other_daemon; then
            log_event "Another scratchircd process exists ($OTHER_PIDS); will not start a duplicate."
        else
            # Recheck on every launch so a removed executable/config is logged.
            if preflight; then
                "$BINARY" "$CONFIG" &
                child=$!
                # A very fast failure may be reaped before we can record it.
                if ! record_pid "$DAEMON" "$child" "$$"; then
                    if proc_info "$child" && [[ $PROC_PARENT == "$$" && $PROC_STATE != Z ]]; then
                        log_event 'Cannot record daemon identity; stopping this child instead of supervising it blindly.'
                        kill -TERM "$child" 2>/dev/null || true
                        wait "$child" 2>/dev/null || true
                        return 1
                    fi
                fi
                log_event "Launched daemon PID $child."
                if (( stopping )) || [[ -e $STOP_FILE ]]; then
                    stopping=1
                    signal_recorded "$DAEMON"
                fi
                while :; do
                    wait "$child"
                    rc=$?
                    # A signal can interrupt wait before the child has exited.
                    pid_alive "$DAEMON" || break
                done
                child=
                rm -f -- "$DAEMON"
                log_event "Daemon exited with status $rc."
            else
                log_event 'Cannot launch with the current paths.'
            fi
        fi
        (( stopping )) && break
        [[ -e $STOP_FILE ]] && break
        log_event "Next launch attempt in $RESTART_DELAY seconds."
        for (( pause=0; pause<RESTART_DELAY; pause++ )); do
            (( stopping )) && break
            [[ -e $STOP_FILE ]] && break
            sleep 1
        done
    done
    log_event 'Stopping supervision.'
}

cleanup_supervisor() {
    trap '' TERM INT
    signal_recorded "$DAEMON"
    if [[ -n ${child:-} ]]; then
        wait "$child" 2>/dev/null || true
    fi
    rm -f -- "$DAEMON" "$SUPERVISOR"
    # The locked file is never unlinked; the kernel releases FD 9 on exit.
}

start_server() {
    local launched attempt
    if lock_held; then
        if [[ ! -e $STOP_FILE ]] && pid_alive "$SUPERVISOR"; then
            printf 'Supervisor already running (PID %s).\n' "$LIVE_PID"
            return 0
        fi
        fail 'A previous supervisor/daemon is stopping or orphaned. Use status, then stop.'
        return 1
    fi
    preflight || return 1
    if other_daemon; then
        fail "Existing scratchircd PID(s): $OTHER_PIDS. Stop that instance before using this launcher."
        return 1
    fi
    # FD 8 serializes control commands, but must not leak to the background job.
    nohup bash "$SELF" __supervise </dev/null >>"$LOG" 2>&1 8>&- &
    launched=$!
    for (( attempt=0; attempt<50; attempt++ )); do
        if pid_alive "$SUPERVISOR" && [[ $LIVE_PID == "$launched" ]]; then
            printf 'Supervisor started (PID %s).\nLog: %s\n' "$launched" "$LOG"
            printf 'This confirms supervision, not IRC readiness; check status and the log.\n'
            return 0
        fi
        kill -0 "$launched" 2>/dev/null || break
        sleep 0.1
    done
    fail "Supervisor did not confirm startup. Inspect $LOG and run status."
}

stop_server() {
    local attempt
    if ! lock_held; then
        printf 'Supervisor and managed daemon are stopped.\n'
        return 0
    fi
    : > "$STOP_FILE" || return 1
    if pid_alive "$SUPERVISOR"; then
        signal_recorded "$SUPERVISOR"
    elif pid_alive "$DAEMON"; then
        printf 'Stopping orphaned managed daemon (PID %s).\n' "$LIVE_PID"
        signal_recorded "$DAEMON"
    else
        fail "Lock is held but no verified managed PID was found. Inspect $LOG; no unrelated process was signalled."
        return 1
    fi
    for (( attempt=0; attempt<STOP_TIMEOUT*10; attempt++ )); do
        if ! lock_held; then
            printf 'Supervisor and managed daemon stopped.\n'
            return 0
        fi
        sleep 0.1
    done
    fail "Still stopping after $STOP_TIMEOUT seconds; no SIGKILL sent and no replacement started. Inspect $LOG."
}

show_status() {
    printf 'Log: %s\n' "$LOG"
    if ! lock_held; then
        printf 'Stopped (no managed supervisor/daemon holds the lock).\n'
        return 3
    fi
    if pid_alive "$SUPERVISOR"; then
        printf 'Supervisor running (PID %s).\n' "$LIVE_PID"
        [[ ! -e $STOP_FILE ]] || printf 'Shutdown requested; auto-restart is disabled.\n'
        if pid_alive "$DAEMON"; then
            printf 'Managed daemon running (PID %s); process presence is not a health check.\n' "$LIVE_PID"
        else
            printf 'No managed daemon currently running; inspect the log for restart attempts.\n'
        fi
        return 0
    fi
    if pid_alive "$DAEMON"; then
        printf 'Orphaned managed daemon (PID %s); auto-restart is NOT active. Use stop, then start.\n' "$LIVE_PID"
    else
        printf 'Lock held without a verified PID; investigate before starting another instance.\n'
    fi
    return 1
}

if [[ $ACTION == __supervise ]]; then
    supervise
    exit $?
fi

# Serialize start/stop/restart/status so concurrent invocations cannot race.
exec 8>"$STATE_DIR/control.lock" || exit 1
flock -w "$((STOP_TIMEOUT + 10))" 8 || { fail 'Another control command is still running.'; exit 1; }
case "$ACTION" in
    start) start_server ;;
    stop) stop_server ;;
    restart) preflight && stop_server && start_server ;;
    status) show_status ;;
esac
