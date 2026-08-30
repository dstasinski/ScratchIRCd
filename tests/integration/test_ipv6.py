#!/usr/bin/env python3
"""End-to-end IPv6 listener and protocol coverage for ScratchIRCd."""

import os
import socket
import subprocess
import sys
import tempfile
import time


class IRCClient:
    def __init__(self, port):
        self.sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        self.sock.settimeout(3.0)
        self.sock.connect(("::1", port))
        assert self.sock.family == socket.AF_INET6
        self.sock.settimeout(0.25)
        self.buffer = b""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def expect(self, needle, duration=4.0):
        deadline = time.monotonic() + duration
        lines = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode(errors="replace")
                lines.append(line)
                if needle in line:
                    return lines
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {lines!r}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def free_port():
    try:
        sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        sock.bind(("::1", 0))
    except OSError as error:
        raise AssertionError("IPv6 loopback is required for the Linux release test") from error
    port = sock.getsockname()[1]
    sock.close()
    return port


def wait_listen(port, proc):
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(proc.stderr.read())
        try:
            probe = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
            probe.settimeout(0.1)
            probe.connect(("::1", port))
            probe.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("IPv6 listener did not start")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick.lower()} 0 * :{nick} User")
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
        raise SystemExit("usage: test_ipv6.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-ipv6-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as handle:
            handle.write("server_name = test.local\nnetwork_name = TestNet\n")
            handle.write("bind_address = ::1\n")
            handle.write(f"port = {port}\nmax_clients = 16\ndns_timeout_seconds = 1\n")
            for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
                handle.write(f"{name}_db = {td}/{name}.db\n")
            handle.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen(
            [binary, conf], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        alice = None
        bob = None
        try:
            wait_listen(port, proc)
            alice = IRCClient(port)
            bob = IRCClient(port)
            register(alice, "Alice")
            register(bob, "Bob")

            alice.send("JOIN #ipv6")
            alice.expect(" 366 Alice #ipv6 ")
            bob.send("JOIN #ipv6")
            bob.expect(" 366 Bob #ipv6 ")
            alice.expect(" JOIN #ipv6")

            alice.send("PRIVMSG #ipv6 :channel over IPv6")
            bob.expect(" PRIVMSG #ipv6 :channel over IPv6")
            bob.send("NOTICE Alice :private over IPv6")
            alice.expect(" NOTICE Alice :private over IPv6")

            bob.send("PING :ipv6-probe")
            bob.expect("PONG test.local ::ipv6-probe")
            assert proc.poll() is None, "server exited while serving IPv6 clients"

            # Shutdown with a live IPv6 connection exercises listener/client
            # cleanup independently of the IPv4 and TLS lifecycle tests.
            alice.close()
            alice = None
            proc.terminate()
            proc.wait(timeout=3.0)
            assert proc.returncode == 0, proc.stderr.read()
        finally:
            if alice is not None:
                alice.close()
            if bob is not None:
                bob.close()
            stop(proc)

    print("IPv6 integration test passed")


if __name__ == "__main__":
    main()
