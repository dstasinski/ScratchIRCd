#!/usr/bin/env python3
"""Run and record the ScratchIRCd small-client operational soak."""

import argparse
import base64
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import select
import socket
import subprocess
import sys
import tempfile
import time


MINIMUM_RELEASE_SOAK_SECONDS = 12 * 60 * 60
IRC_NICK_MAX = 15
IRC_USER_MAX = 10
IRC_CHANNEL_NAME_MAX = 32
MILESTONE2_CAPABILITIES = (
    "account-notify away-notify batch draft/chathistory extended-join "
    "labeled-response message-tags sasl server-time"
)
SOAK_ADMIN_PASSWORD = "scratchircd-milestone2-soak-admin"
SOAK_ACCOUNT_PASSWORD = "scratchircd-milestone2-soak-account"


class IRCClient:
    def __init__(self, port, nick):
        self.nick = nick
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
        self.sock.setblocking(False)
        self.buffer = b""
        self.closed = False

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode("utf-8"))

    def _control_reply(self, line):
        payload = line
        prefix = ""
        if payload.startswith("@") and " " in payload:
            payload = payload.split(" ", 1)[1]
        if payload.startswith(":") and " " in payload:
            prefix, payload = payload.split(" ", 1)
        if payload.startswith("PING :"):
            self.send("PONG :" + payload.split(":", 1)[1])
        if payload == f"PRIVMSG {self.nick} :\x01VERSION\x01":
            server_name = prefix[1:] if prefix.startswith(":") else "soak.local"
            self.send(f"NOTICE {server_name} :\x01VERSION ScratchIRCd-soak\x01")

    def read(self, timeout=0.0):
        lines = []
        if self.closed:
            return lines
        readable, _, _ = select.select([self.sock], [], [], timeout)
        while readable:
            try:
                data = self.sock.recv(65536)
            except BlockingIOError:
                break
            if not data:
                self.closed = True
                break
            self.buffer += data
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode("utf-8", errors="replace")
                lines.append(line)
                self._control_reply(line)
            readable, _, _ = select.select([self.sock], [], [], 0.0)
        return lines

    def expect(self, needle, timeout=5.0):
        deadline = time.monotonic() + timeout
        seen = []
        while time.monotonic() < deadline:
            lines = self.read(min(0.1, deadline - time.monotonic()))
            seen.extend(lines)
            if any(needle in line for line in lines):
                return seen
            if self.closed:
                break
        raise RuntimeError(f"{self.nick}: expected {needle!r}; got {seen!r}")

    def expect_sequence(self, needles, timeout=5.0):
        """Wait for ordered response fragments without dropping coalesced lines."""
        deadline = time.monotonic() + timeout
        seen = []
        matched = 0
        while time.monotonic() < deadline:
            lines = self.read(min(0.1, deadline - time.monotonic()))
            seen.extend(lines)
            for line in lines:
                if needles[matched] in line:
                    matched += 1
                    if matched == len(needles):
                        return seen
            if self.closed:
                break
        raise RuntimeError(
            f"{self.nick}: expected sequence {needles!r}; got {seen!r}"
        )

    def close(self, orderly=False):
        if orderly and not self.closed:
            try:
                self.send("QUIT :soak churn")
            except OSError:
                pass
        try:
            self.sock.close()
        except OSError:
            pass
        self.closed = True


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run ScratchIRCd under small-client churn and record resource growth."
    )
    parser.add_argument("binary", help="path to the scratchircd executable")
    duration = parser.add_mutually_exclusive_group()
    duration.add_argument("--duration-hours", type=float, default=12.0)
    duration.add_argument("--duration-seconds", type=float)
    parser.add_argument("--clients", type=int, default=12,
                        help="number of stable clients (default: 12)")
    parser.add_argument("--cycle-delay-seconds", type=float, default=1.0)
    parser.add_argument("--sample-interval-seconds", type=float, default=60.0)
    parser.add_argument("--max-rss-growth-kib", type=int, default=32768)
    parser.add_argument("--max-fd-growth", type=int, default=16)
    parser.add_argument(
        "--release-candidate",
        action="store_true",
        help=(
            "require a clean checkout, a strict Release build, and a soak of "
            "at least 12 hours"
        ),
    )
    parser.add_argument("--report", help="write the JSON evidence record to this path")
    return parser.parse_args()


def utc_now():
    return dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")


def command_output(command, cwd=None):
    try:
        result = subprocess.run(command, cwd=cwd, check=False, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=10)
    except (OSError, subprocess.TimeoutExpired) as error:
        return f"unavailable: {error}"
    output = result.stdout.strip()
    return output if output else f"exit status {result.returncode}"


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def build_metadata(binary):
    cache_path = binary.parent / "CMakeCache.txt"
    selected = {}
    if cache_path.is_file():
        wanted = {
            "CMAKE_BUILD_TYPE",
            "CMAKE_C_COMPILER",
            "SCRATCHIRCD_ENABLE_SANITIZERS",
            "SCRATCHIRCD_WARNINGS_AS_ERRORS",
        }
        for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("//") or line.startswith("#") or "=" not in line:
                continue
            key_and_type, value = line.split("=", 1)
            key = key_and_type.split(":", 1)[0]
            if key in wanted:
                selected[key] = value
    compiler = selected.get("CMAKE_C_COMPILER", os.environ.get("CC", "cc"))
    return {
        "cmake_cache": selected,
        "compiler": command_output([compiler, "--version"]),
    }


def git_metadata(repository):
    try:
        commit_result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repository,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=10,
        )
        status_result = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=repository,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return {
            "commit": f"unavailable: {error}",
            "status": f"unavailable: {error}",
            "clean": False,
        }

    commit = commit_result.stdout.strip()
    status = status_result.stdout.strip()
    usable = commit_result.returncode == 0 and status_result.returncode == 0
    return {
        "commit": commit if commit else f"exit status {commit_result.returncode}",
        "status": status,
        "clean": usable and not status,
    }


def release_preflight_failures(duration, build, repository_state):
    failures = []
    if duration < MINIMUM_RELEASE_SOAK_SECONDS:
        failures.append(
            "release-candidate soak must run for at least "
            f"{MINIMUM_RELEASE_SOAK_SECONDS} seconds"
        )
    if not repository_state["clean"]:
        detail = repository_state["status"] or "Git metadata unavailable"
        failures.append(f"release-candidate checkout is not clean: {detail}")

    cache = build["cmake_cache"]
    if cache.get("CMAKE_BUILD_TYPE") != "Release":
        failures.append("release-candidate binary was not built with CMAKE_BUILD_TYPE=Release")
    if cache.get("SCRATCHIRCD_WARNINGS_AS_ERRORS") != "ON":
        failures.append(
            "release-candidate binary was not built with "
            "SCRATCHIRCD_WARNINGS_AS_ERRORS=ON"
        )
    return failures


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def wait_listen(port, process):
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"daemon exited during startup with {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("daemon did not begin listening within 10 seconds")


def write_config(path, directory, port, max_clients, admin_hash):
    motd = directory / "motd.txt"
    rules = directory / "rules.txt"
    motd.write_text("ScratchIRCd Milestone 2 soak\n", encoding="utf-8")
    rules.write_text(
        "Exercise account, channel-service, IRCv3, and lifecycle behavior.\n",
        encoding="utf-8",
    )
    lines = [
        "server_name = soak.local",
        "network_name = SoakNet",
        "bind_address = 127.0.0.1",
        f"port = {port}",
        f"max_clients = {max_clients}",
        "max_channels = 32",
        "max_connections_per_ip = 0",
        "registration_timeout_seconds = 15",
        "ping_interval_seconds = 5",
        "ping_timeout_seconds = 5",
        "output_queue_max_bytes = 65536",
        "dns_timeout_seconds = 1",
        "nospoof = yes",
        "nospoof_timeout_seconds = 5",
        "geoip_city_db =",
        "geoip_asn_db =",
        "netadmin_name = root",
        f"netadmin_password_hash = {admin_hash}",
        "netadmin_hostmask = *!*@127.0.0.1",
        f"motd_file = {motd}",
        f"rules_file = {rules}",
    ]
    for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
        lines.append(f"{name}_db = {directory / (name + '.db')}")
    text = "\n".join(lines) + "\n"
    path.write_text(text, encoding="utf-8")
    return text


def make_password_hash(binary, password):
    """Create an Argon2id hash with the helper built beside the daemon."""
    helper = binary.with_name("scratchircd-mkpasswd")
    if not helper.is_file() or not os.access(helper, os.X_OK):
        raise RuntimeError(f"required password helper is unavailable: {helper}")
    try:
        result = subprocess.run(
            [str(helper), password],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RuntimeError(f"password helper failed: {error}") from error
    encoded = result.stdout.strip()
    if result.returncode != 0 or not encoded.startswith("$argon2id$"):
        raise RuntimeError(
            f"password helper failed with status {result.returncode}: {encoded}"
        )
    return encoded


def register(port, nick, user=None, channel="#soak", capabilities=None):
    client = IRCClient(port, nick)
    try:
        if user is None:
            user = nick[:IRC_USER_MAX]
        if capabilities is not None:
            client.send("CAP LS 302")
            client.expect(" CAP * LS :")
            client.send(f"CAP REQ :{capabilities}")
            client.expect(f" ACK :{capabilities}")
        client.send(f"NICK {nick}")
        client.send(f"USER {user} 0 * :{nick} soak client")
        if capabilities is not None:
            client.send("CAP END")
        client.expect(f" 001 {nick} ", 8.0)
        # Registration follows the matching no-spoof PONG, but JOIN remains
        # restricted until the server's CTCP VERSION request is answered.
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            client.read(min(0.1, deadline - time.monotonic()))
            client.send(f"JOIN {channel}")
            try:
                client.expect(f" 366 {nick} {channel} ", 0.3)
                return client
            except RuntimeError:
                continue
        raise RuntimeError(f"{nick}: CTCP VERSION restriction did not release")
    except Exception:
        client.close()
        raise


def validate_protocol_limits(port):
    maximum_nick = "N" * IRC_NICK_MAX
    maximum_user = "u" * IRC_USER_MAX
    maximum_channel = "#" + ("c" * (IRC_CHANNEL_NAME_MAX - 1))
    boundary = register(port, maximum_nick, maximum_user, maximum_channel)
    try:
        boundary.send(f"MODE {maximum_nick}")
        boundary.expect(f" 221 {maximum_nick} +x")
        boundary.send("JOIN #" + ("x" * IRC_CHANNEL_NAME_MAX))
        boundary.expect(f" 403 {maximum_nick} ")
    finally:
        boundary.close(orderly=True)

    overlong_nick = IRCClient(port, "overlong-nick")
    try:
        overlong_nick.send("NICK " + ("N" * (IRC_NICK_MAX + 1)))
        overlong_nick.expect(" 432 * ")
    finally:
        overlong_nick.close()

    overlong_user = IRCClient(port, "overlong-user")
    try:
        overlong_user.send("NICK UserLimit")
        overlong_user.send(
            "USER " + ("u" * (IRC_USER_MAX + 1)) + " 0 * :Overlong User"
        )
        overlong_user.expect(" 468 UserLimit :Invalid USER identity")
    finally:
        overlong_user.close()


def provision_milestone2(stable, coverage):
    """Establish persistent services state and verify live account signaling."""
    owner = stable[0]
    observer = stable[1]

    owner.send(f"NICKSERV REGISTER {SOAK_ACCOUNT_PASSWORD}")
    owner.expect("Nickname registered and identified.", 15.0)
    observer.expect(" ACCOUNT S000", 5.0)
    coverage["nickserv_registrations"] += 1
    coverage["account_notify_events"] += 1

    observer.send("AUTHENTICATE PLAIN")
    observer.expect(" AUTHENTICATE +")
    plain = base64.b64encode(
        f"\0S000\0{SOAK_ACCOUNT_PASSWORD}".encode("utf-8")
    ).decode("ascii")
    observer.send(f"AUTHENTICATE {plain}")
    observer.expect(" 903 S001 ", 15.0)
    owner.expect(" ACCOUNT S000", 5.0)
    coverage["sasl_authentications"] += 1
    coverage["account_notify_events"] += 1

    owner.send(f"OPER root {SOAK_ADMIN_PASSWORD}")
    owner.expect(" 381 S000 :You are now a Network Administrator", 15.0)
    owner.send("CHANSERV REGISTER #soak :Milestone 2 operational soak")
    owner.expect("Channel registered successfully.")
    coverage["chanserv_registrations"] += 1

    owner.send("CHANSERV SET #soak MLOCK +nt")
    owner.expect("Persistent mode lock updated.")
    owner.send("CHANSERV SET #soak TOPIC :Milestone 2 operational soak")
    owner.expect("Persistent topic updated.")
    owner.send("TOPIC #soak")
    owner.expect(" 332 S000 #soak :Milestone 2 operational soak")
    owner.send("MODE #soak")
    mode_lines = owner.expect(" 324 S000 #soak ")
    if not any(
        all(mode in line.rsplit(" ", 1)[-1] for mode in ("n", "r", "t"))
        for line in mode_lines
        if " 324 S000 #soak " in line
    ):
        raise RuntimeError(f"S000: ChanServ MLOCK was not active: {mode_lines!r}")
    coverage["chanserv_mode_locks"] += 1
    coverage["chanserv_topics"] += 1
def process_sample(pid, started):
    status_path = Path(f"/proc/{pid}/status")
    fd_path = Path(f"/proc/{pid}/fd")
    values = {}
    for line in status_path.read_text(encoding="utf-8").splitlines():
        if line.startswith(("VmRSS:", "VmSize:", "Threads:")):
            key, value = line.split(":", 1)
            values[key] = int(value.strip().split()[0])
    return {
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "rss_kib": values["VmRSS"],
        "virtual_kib": values["VmSize"],
        "threads": values["Threads"],
        "fd_count": len(list(fd_path.iterdir())),
    }


def drain_clients(clients):
    for client in clients:
        client.read(0.0)
        if client.closed:
            raise RuntimeError(f"stable client {client.nick} was disconnected")


def run_cycle(port, stable, cycle, counters, coverage):
    sender = stable[cycle % len(stable)]
    target = stable[(cycle + 1) % len(stable)]
    token = f"soak-{cycle}"
    sender.send(f"@+soak-cycle={cycle} PRIVMSG #soak :channel {cycle}")
    tagged = target.expect(f" PRIVMSG #soak :channel {cycle}", 2.0)
    tagged_line = next(
        (line for line in tagged if f" PRIVMSG #soak :channel {cycle}" in line),
        "",
    )
    if f"+soak-cycle={cycle}" not in tagged_line or not tagged_line.startswith("@time="):
        raise RuntimeError(
            f"{target.nick}: tagged/server-time delivery was incomplete: {tagged_line!r}"
        )
    sender.send(f"PRIVMSG {target.nick} :private {cycle}")
    # PING's handler adds the trailing-parameter colon to PONG. Supplying one
    # here would make the echoed token `::soak-N` and break the exact wire
    # assertion below.
    sender.send(f"@label={token} PING {token}")
    sender.expect_sequence(
        [f"label={token}", f" PONG soak.local :{token}", " BATCH -"], 2.0
    )
    counters["channel_messages"] += 1
    counters["private_messages"] += 1
    counters["health_pings"] += 1
    coverage["tagged_messages"] += 1
    coverage["server_time_messages"] += 1
    coverage["labeled_responses"] += 1

    if cycle % 10 == 0:
        history_client = stable[(cycle + 2) % len(stable)]
        history_client.send("CHATHISTORY LATEST #soak * 1")
        history_client.expect_sequence(
            [" chathistory #soak", f" PRIVMSG #soak :channel {cycle}", " BATCH -"],
            3.0,
        )
        coverage["history_requests"] += 1

    churn = register(port, f"C{cycle % 1000000:06d}")
    try:
        target.expect(f" JOIN #soak * :C{cycle % 1000000:06d} soak client", 2.0)
        coverage["extended_join_events"] += 1
        churn.send(f"AWAY :soak cycle {cycle}")
        churn.expect(f" 306 C{cycle % 1000000:06d} ")
        target.expect(f" AWAY :soak cycle {cycle}", 2.0)
        coverage["away_notify_events"] += 1
        churn.send(f"PRIVMSG #soak :churn {cycle}")
        churn.send("PART #soak :cycle complete")
        churn.read(0.05)
    finally:
        churn.close(orderly=(cycle % 2 == 0))
    counters["churn_connections"] += 1
    drain_clients(stable)


def tail(path, limit=40):
    try:
        return path.read_text(encoding="utf-8", errors="replace").splitlines()[-limit:]
    except OSError:
        return []


def emit_report(report, destination_name):
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if destination_name:
        destination = Path(destination_name).resolve()
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(encoded, encoding="utf-8")
        print(f"soak evidence: {destination}")
    summary = report.get("resource_summary", {})
    print(
        f"soak {'passed' if report['passed'] else 'failed'}: "
        f"elapsed={report['elapsed_seconds']}s churn={report['traffic']['churn_connections']} "
        f"rss_growth={summary.get('rss_growth_kib', 'n/a')}KiB "
        f"fd_growth={summary.get('fd_growth', 'n/a')}"
    )
    for failure in report["failures"]:
        print(f"failure: {failure}", file=sys.stderr)


def main():
    args = parse_args()
    binary = Path(args.binary).resolve()
    duration = args.duration_seconds if args.duration_seconds is not None else args.duration_hours * 3600.0
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise SystemExit(f"not an executable file: {binary}")
    if duration <= 0 or args.clients < 2 or args.sample_interval_seconds <= 0:
        raise SystemExit("duration and sample interval must be positive; clients must be at least 2")
    if args.cycle_delay_seconds < 0 or args.max_rss_growth_kib < 0 or args.max_fd_growth < 0:
        raise SystemExit("cycle delay and growth thresholds cannot be negative")
    if not Path("/proc/self/status").exists():
        raise SystemExit("the soak runner requires Linux /proc process metrics")

    started = time.monotonic()
    repository = Path(__file__).resolve().parents[1]
    build = build_metadata(binary)
    repository_state = git_metadata(repository)
    report = {
        "schema": "scratchircd-milestone2-soak-v1",
        "started_at": utc_now(),
        "requested_duration_seconds": duration,
        "stable_clients": args.clients,
        "thresholds": {
            "max_rss_growth_kib": args.max_rss_growth_kib,
            "max_fd_growth": args.max_fd_growth,
        },
        "binary": {
            "path": str(binary),
            "sha256": sha256_file(binary),
        },
        "environment": {
            "git_commit": repository_state["commit"],
            "git_status": repository_state["status"],
            "git_clean": repository_state["clean"],
            "platform": platform.platform(),
            "python": platform.python_version(),
            "compiler": build["compiler"],
            "cmake_cache": build["cmake_cache"],
            "cmake": command_output(["cmake", "--version"]),
            "openssl": command_output(["openssl", "version"]),
            "linked_libraries": command_output(["ldd", str(binary)]),
        },
        "traffic": {
            "channel_messages": 0,
            "private_messages": 0,
            "health_pings": 0,
            "churn_connections": 0,
        },
        "protocol_limits": {
            "nick": IRC_NICK_MAX,
            "user": IRC_USER_MAX,
            "channel_name": IRC_CHANNEL_NAME_MAX,
            "validated": False,
        },
        "milestone2_coverage": {
            "nickserv_registrations": 0,
            "sasl_authentications": 0,
            "account_notify_events": 0,
            "chanserv_registrations": 0,
            "chanserv_mode_locks": 0,
            "chanserv_topics": 0,
            "away_notify_events": 0,
            "extended_join_events": 0,
            "tagged_messages": 0,
            "labeled_responses": 0,
            "server_time_messages": 0,
            "history_requests": 0,
        },
        "samples": [],
        "failures": [],
        "passed": False,
        "release_qualified": False,
        "qualification": {
            "requested": args.release_candidate,
            "minimum_duration_seconds": MINIMUM_RELEASE_SOAK_SECONDS,
            "preflight_passed": None,
        },
    }

    preflight_failures = []
    if args.release_candidate:
        preflight_failures = release_preflight_failures(
            duration, build, repository_state
        )
        report["qualification"]["preflight_passed"] = not preflight_failures
    if preflight_failures:
        report["failures"].extend(preflight_failures)
        report["completed_at"] = utc_now()
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        report["shutdown_exit_status"] = None
        emit_report(report, args.report)
        return 1

    process = None
    stable = []
    shutdown_exit_status = None
    with tempfile.TemporaryDirectory(prefix="scratchircd-soak-") as temporary:
        directory = Path(temporary)
        config = directory / "ircd.conf"
        log_path = directory / "scratchircd.log"
        port = free_port()
        try:
            admin_hash = make_password_hash(binary, SOAK_ADMIN_PASSWORD)
            configuration_text = write_config(
                config, directory, port, args.clients + 16, admin_hash
            )
            report["configuration"] = configuration_text.replace(
                admin_hash, "<redacted>"
            )
            with open(log_path, "w", encoding="utf-8") as log_handle:
                process = subprocess.Popen([str(binary), str(config)], stdout=log_handle,
                                           stderr=subprocess.STDOUT, text=True)
                wait_listen(port, process)
                validate_protocol_limits(port)
                report["protocol_limits"]["validated"] = True
                for index in range(args.clients):
                    stable.append(
                        register(
                            port,
                            f"S{index:03d}",
                            capabilities=MILESTONE2_CAPABILITIES,
                        )
                    )
                drain_clients(stable)
                provision_milestone2(stable, report["milestone2_coverage"])
                drain_clients(stable)
                time.sleep(0.2)
                report["samples"].append(process_sample(process.pid, started))
                baseline = report["samples"][0]
                workload_started = time.monotonic()
                next_sample = workload_started + args.sample_interval_seconds
                deadline = workload_started + duration
                cycle = 0
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f"daemon exited unexpectedly with {process.returncode}")
                    run_cycle(
                        port,
                        stable,
                        cycle,
                        report["traffic"],
                        report["milestone2_coverage"],
                    )
                    cycle += 1
                    now = time.monotonic()
                    if now >= next_sample:
                        report["samples"].append(process_sample(process.pid, started))
                        next_sample = now + args.sample_interval_seconds
                    remaining = deadline - time.monotonic()
                    if remaining > 0:
                        time.sleep(min(args.cycle_delay_seconds, remaining))
                time.sleep(0.2)
                drain_clients(stable)
                report["samples"].append(process_sample(process.pid, started))
        except (OSError, RuntimeError) as error:
            report["failures"].append(str(error))
        finally:
            for client in stable:
                client.close(orderly=True)
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    shutdown_exit_status = process.wait(timeout=10.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    shutdown_exit_status = process.wait(timeout=3.0)
                    report["failures"].append("daemon did not complete graceful SIGTERM shutdown")
            elif process is not None:
                shutdown_exit_status = process.returncode
            report["daemon_log_tail"] = tail(log_path)

        if report["samples"]:
            baseline = report["samples"][0]
            final = report["samples"][-1]
            maximum_rss = max(sample["rss_kib"] for sample in report["samples"])
            maximum_fds = max(sample["fd_count"] for sample in report["samples"])
            report["resource_summary"] = {
                "baseline_rss_kib": baseline["rss_kib"],
                "final_rss_kib": final["rss_kib"],
                "maximum_rss_kib": maximum_rss,
                "rss_growth_kib": max(0, final["rss_kib"] - baseline["rss_kib"]),
                "baseline_fd_count": baseline["fd_count"],
                "final_fd_count": final["fd_count"],
                "maximum_fd_count": maximum_fds,
                "fd_growth": max(0, final["fd_count"] - baseline["fd_count"]),
            }
            if maximum_rss - baseline["rss_kib"] > args.max_rss_growth_kib:
                report["failures"].append(
                    f"RSS growth exceeded {args.max_rss_growth_kib} KiB: "
                    f"baseline={baseline['rss_kib']} maximum={maximum_rss}"
                )
            if maximum_fds - baseline["fd_count"] > args.max_fd_growth:
                report["failures"].append(
                    f"file-descriptor growth exceeded {args.max_fd_growth}: "
                    f"baseline={baseline['fd_count']} maximum={maximum_fds}"
                )
        else:
            report["failures"].append("no process resource samples were collected")

    report["completed_at"] = utc_now()
    report["elapsed_seconds"] = round(time.monotonic() - started, 3)
    report["shutdown_exit_status"] = shutdown_exit_status
    if shutdown_exit_status not in (0, None):
        report["failures"].append(f"daemon shutdown exit status was {shutdown_exit_status}")
    if report["traffic"]["churn_connections"] == 0:
        report["failures"].append("no churn cycle completed")
    missing_coverage = [
        name for name, count in report["milestone2_coverage"].items() if count == 0
    ]
    if missing_coverage:
        report["failures"].append(
            "Milestone 2 coverage did not complete: " + ", ".join(missing_coverage)
        )
    report["passed"] = not report["failures"]
    report["release_qualified"] = args.release_candidate and report["passed"]
    emit_report(report, args.report)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
