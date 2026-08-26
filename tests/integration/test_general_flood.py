#!/usr/bin/env python3
"""End-to-end coverage for the general inbound command flood budget."""

import os
import socket
import subprocess
import sys
import tempfile
import time


class IRCClient:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
        self.sock.settimeout(0.25)
        self.buffer = b""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def expect(self, needle, duration=5.0):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode(errors="replace")
                got.append(line)
                if needle in line:
                    return line
            try:
                data = self.sock.recv(8192)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {got!r}")

    def closed(self, duration=3.0):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(8192)
                if not data:
                    return True
            except socket.timeout:
                continue
            except (ConnectionResetError, OSError):
                return True
        return False

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def free_port():
    sock = socket.socket()
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
            sock = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            sock.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("listener did not start")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick}")
    client.expect(f" 001 {nick} ")


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
        raise SystemExit("usage: test_general_flood.py scratchircd")

    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-general-flood-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as handle:
            handle.write("server_name = test.local\nnetwork_name = TestNet\n")
            handle.write("bind_address = 127.0.0.1\n")
            handle.write(f"port = {port}\nmax_clients = 16\ndns_timeout_seconds = 1\n")
            for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
                handle.write(f"{name}_db = {td}/{name}.db\n")
            handle.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen(
            [binary, conf], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        client = None
        try:
            wait_listen(port, proc)
            client = IRCClient(port)
            register(client, "Flooder")

            # A normal burst of inexpensive commands must be tolerated.
            for index in range(20):
                client.send(f"PING :normal-{index}")
            client.expect("PONG :normal-19")

            # PONG is explicitly exempt because it answers server liveness probes.
            for index in range(100):
                client.send(f"PONG :server-{index}")
            client.send("PING :after-pongs")
            client.expect("PONG :after-pongs")

            # Unknown commands count against the same aggregate budget. Once the
            # bucket is exhausted, two commands are throttled and sustained abuse
            # on the third violation disconnects the client.
            for index in range(120):
                try:
                    client.send(f"BOGUS{index}")
                except OSError:
                    break

            client.expect("ERROR :Excess flood")
            assert client.closed(), "flooding client was not disconnected"
        finally:
            if client is not None:
                client.close()
            stop(proc)


if __name__ == "__main__":
    main()
