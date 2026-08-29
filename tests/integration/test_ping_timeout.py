#!/usr/bin/env python3
"""Server-initiated PING/PONG liveness and timeout behavior."""

import os
import socket
import subprocess
import sys
import tempfile
import time


class IRCClient:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
        self.sock.settimeout(0.2)
        self.buffer = b""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def read_lines(self, duration=0.2):
        deadline = time.monotonic() + duration
        lines = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                lines.append(raw.rstrip(b"\r").decode(errors="replace"))
            try:
                data = self.sock.recv(8192)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            lines.append(raw.rstrip(b"\r").decode(errors="replace"))
        return lines

    def expect(self, needle, duration=5.0, auto_pong=False):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
            for line in self.read_lines(0.1):
                seen.append(line)
                if auto_pong and line.startswith("PING :"):
                    self.send(f"PONG :{line[6:]}")
                if needle in line:
                    return line, seen
        raise AssertionError(f"expected {needle!r}; got {seen!r}")

    def wait_server_ping(self, duration=5.0):
        line, _ = self.expect("PING :", duration)
        if not line.startswith("PING :"):
            raise AssertionError(f"expected server PING; got {line!r}")
        return line[6:]

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def free_port():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def wait_listen(port, proc):
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(proc.stderr.read())
        try:
            probe = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            probe.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not begin listening")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


def answer_pending_pings(client):
    for line in client.read_lines(0.3):
        if line.startswith("PING :"):
            client.send(f"PONG :{line[6:]}")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3.0)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_ping_timeout.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-ping-timeout-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as handle:
            handle.write("server_name = test.local\nnetwork_name = TestNet\n")
            handle.write("bind_address = 127.0.0.1\n")
            handle.write(f"port = {port}\nmax_clients = 16\n")
            handle.write("dns_timeout_seconds = 1\nregistration_timeout_seconds = 10\n")
            handle.write("ping_interval_seconds = 3\nping_timeout_seconds = 2\n")
            handle.write("nospoof = no\ngeoip_city_db = \ngeoip_asn_db = \n")
            for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
                handle.write(f"{name}_db = {td}/{name}.db\n")

        proc = subprocess.Popen(
            [binary, conf], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        responsive = None
        silent = None
        try:
            wait_listen(port, proc)
            responsive = IRCClient(port)
            register(responsive, "Responsive")

            # Complete commands of any kind keep a client out of the idle state.
            for index in range(4):
                time.sleep(0.7)
                responsive.send(f"PING :activity-{index}")
                _, seen = responsive.expect(f"PONG test.local ::activity-{index}")
                assert not any(line.startswith("PING :") for line in seen), seen

            # A matching PONG clears the outstanding challenge and starts a new
            # idle interval. Remaining connected beyond the old deadline proves
            # that the matching response was consumed.
            token = responsive.wait_server_ping()
            responsive.send(f"PONG :{token}")
            time.sleep(1.2)
            responsive.send("PING :probe-one")
            responsive.expect("PONG test.local ::probe-one")
            time.sleep(1.2)
            responsive.send("PING :probe-two")
            responsive.expect("PONG test.local ::probe-two")

            responsive.send("JOIN #liveness")
            responsive.expect(" 366 Responsive #liveness ")
            silent = IRCClient(port)
            register(silent, "Silent")
            silent.send("JOIN #liveness")
            silent.expect(" 366 Silent #liveness ")

            # A wrong PONG and unrelated traffic do not replace the required
            # matching PONG. Other channel members see the exact timeout reason.
            responsive.send("PING :observer-reset")
            responsive.expect("PONG test.local ::observer-reset")
            silent_token = silent.wait_server_ping()
            assert silent_token != token
            answer_pending_pings(responsive)
            silent.send("PONG :wrong-token")
            silent.send("PRIVMSG Responsive :not-a-pong")
            responsive.expect("PRIVMSG Responsive :not-a-pong", auto_pong=True)
            silent.expect("ERROR :Ping Timeout: 2 seconds", duration=5.0)
            responsive.expect("QUIT :Ping Timeout: 2 seconds", duration=5.0,
                              auto_pong=True)

            responsive.send("PING :after-timeout")
            responsive.expect("PONG test.local ::after-timeout", auto_pong=True)
            assert proc.poll() is None, "server exited during PING timeout handling"
        finally:
            if silent is not None:
                silent.close()
            if responsive is not None:
                responsive.close()
            stop(proc)

    print("server PING timeout tests passed")


if __name__ == "__main__":
    main()
