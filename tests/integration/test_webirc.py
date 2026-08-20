#!/usr/bin/env python3
"""End-to-end WebIRC gateway integration coverage for ScratchIRCd."""

import os
import socket
import subprocess
import sys
import tempfile
import time


class IRCClient:
    def __init__(self, sock):
        self.sock = sock
        self.sock.settimeout(0.25)
        self.buffer = b""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def expect(self, needle, duration=4.0):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode(errors="replace")
                got.append(line)
                if needle in line:
                    return got
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {got!r}")

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
            s = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            s.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("listener did not start")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_webirc.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-webirc-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\n")
            f.write("max_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")
            f.write("webirc_gateway = 127.0.0.1 gateway-secret\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        trusted = None
        rejected = None
        try:
            wait_listen(port, proc)

            trusted = IRCClient(socket.create_connection(("127.0.0.1", port), timeout=3.0))
            trusted.send("WEBIRC gateway-secret web.example supplied.example 203.0.113.9")
            trusted.send("NICK webuser")
            trusted.send("USER webuser 0 * :Web User")
            welcome = trusted.expect(" 001 webuser ", duration=5.0)
            assert any("webuser!webuser@203.0.113.9" in line for line in welcome), welcome

            trusted.send("MODE webuser")
            modes = trusted.expect(" 221 webuser ")
            assert any("V" in line.rsplit(" ", 1)[-1]
                       for line in modes if " 221 webuser " in line), modes

            rejected = IRCClient(socket.create_connection(("127.0.0.1", port), timeout=3.0))
            rejected.send("WEBIRC wrong-password web.example supplied.example 198.51.100.5")
            rejected.expect("Unauthorized WEBIRC gateway")
            try:
                rejected.send("NICK badweb")
                rejected.send("USER badweb 0 * :Bad WebIRC")
            except OSError:
                pass

        finally:
            if trusted is not None:
                trusted.close()
            if rejected is not None:
                rejected.close()
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=2)

    print("WebIRC integration test passed")


if __name__ == "__main__":
    main()
