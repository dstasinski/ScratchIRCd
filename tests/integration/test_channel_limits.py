#!/usr/bin/env python3
"""End-to-end coverage for the bounded live-channel pool."""

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

    def expect(self, needle, duration=5.0):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode(errors="replace")
                seen.append(line)
                if needle in line:
                    return line
            try:
                data = self.sock.recv(8192)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {seen!r}")

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
        raise SystemExit("usage: test_channel_limits.py scratchircd")

    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-channel-limit-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as handle:
            handle.write("server_name = test.local\nnetwork_name = TestNet\n")
            handle.write("bind_address = 127.0.0.1\n")
            handle.write(f"port = {port}\nmax_clients = 8\nmax_channels = 2\n")
            handle.write("dns_timeout_seconds = 1\n")
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
            client.send("NICK LimitUser")
            client.send("USER limit 0 * :Channel Limit User")
            client.expect(" 001 LimitUser ")

            client.send("JOIN #one")
            client.expect(" 366 LimitUser #one ")
            client.send("JOIN #two")
            client.expect(" 366 LimitUser #two ")

            # The third distinct live channel would exceed max_channels=2.
            client.send("JOIN #three")
            client.expect("Cannot create #three: server channel limit reached (2)")

            # Empty channels are destroyed, so PART returns capacity immediately.
            client.send("PART #one :free-slot")
            client.expect(" PART #one :free-slot")
            client.send("JOIN #three")
            client.expect(" 366 LimitUser #three ")
        finally:
            if client is not None:
                client.close()
            stop(proc)


if __name__ == "__main__":
    main()
