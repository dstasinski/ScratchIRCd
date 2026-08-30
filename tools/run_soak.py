#!/usr/bin/env python3
"""Run and record the Milestone 1 small-client operational soak."""

import argparse
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
        if line.startswith("PING :"):
            self.send("PONG :" + line.split(":", 1)[1])
        if (" PRIVMSG " + self.nick + " :\x01VERSION\x01") in line:
            prefix = line.split(" ", 1)[0]
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


def write_config(path, directory, port, max_clients):
    motd = directory / "motd.txt"
    rules = directory / "rules.txt"
    motd.write_text("ScratchIRCd Milestone 1 soak\n", encoding="utf-8")
    rules.write_text("Exercise bounded lifecycle behavior.\n", encoding="utf-8")
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
        f"motd_file = {motd}",
        f"rules_file = {rules}",
    ]
    for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
        lines.append(f"{name}_db = {directory / (name + '.db')}")
    text = "\n".join(lines) + "\n"
    path.write_text(text, encoding="utf-8")
    return text


def register(port, nick):
    client = IRCClient(port, nick)
    try:
        client.send(f"NICK {nick}")
        client.send(f"USER {nick} 0 * :{nick} soak client")
        client.expect(f" 001 {nick} ", 8.0)
        # Registration follows the matching no-spoof PONG, but JOIN remains
        # restricted until the server's CTCP VERSION request is answered.
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            client.read(min(0.1, deadline - time.monotonic()))
            client.send("JOIN #soak")
            try:
                client.expect(f" 366 {nick} #soak ", 0.3)
                return client
            except RuntimeError:
                continue
        raise RuntimeError(f"{nick}: CTCP VERSION restriction did not release")
    except Exception:
        client.close()
        raise


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


def run_cycle(port, stable, cycle, counters):
    sender = stable[cycle % len(stable)]
    target = stable[(cycle + 1) % len(stable)]
    token = f"soak-{cycle}"
    sender.send(f"PRIVMSG #soak :channel {cycle}")
    sender.send(f"PRIVMSG {target.nick} :private {cycle}")
    sender.send(f"PING :{token}")
    sender.expect(token, 2.0)
    counters["channel_messages"] += 1
    counters["private_messages"] += 1
    counters["health_pings"] += 1

    churn = register(port, f"C{cycle % 1000000:06d}")
    try:
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

    repository = Path(__file__).resolve().parents[1]
    build = build_metadata(binary)
    report = {
        "schema": "scratchircd-milestone1-soak-v1",
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
            "git_commit": command_output(["git", "rev-parse", "HEAD"], repository),
            "git_status": command_output(["git", "status", "--short"], repository),
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
        "samples": [],
        "failures": [],
        "passed": False,
    }

    process = None
    stable = []
    shutdown_exit_status = None
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="scratchircd-soak-") as temporary:
        directory = Path(temporary)
        config = directory / "ircd.conf"
        log_path = directory / "scratchircd.log"
        port = free_port()
        report["configuration"] = write_config(config, directory, port, args.clients + 16)
        try:
            with open(log_path, "w", encoding="utf-8") as log_handle:
                process = subprocess.Popen([str(binary), str(config)], stdout=log_handle,
                                           stderr=subprocess.STDOUT, text=True)
                wait_listen(port, process)
                for index in range(args.clients):
                    stable.append(register(port, f"S{index:03d}"))
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
                    run_cycle(port, stable, cycle, report["traffic"])
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
    report["passed"] = not report["failures"]

    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.report:
        destination = Path(args.report).resolve()
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
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
