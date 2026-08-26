#!/usr/bin/env python3
import os
import socket
import subprocess
import sys
import tempfile
import time

class IRC:
    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.s.settimeout(0.2)
        self.buf = b""

    def send(self, line):
        self.s.sendall((line + "\r\n").encode())

    def lines(self, duration=0.5):
        out = []
        end = time.monotonic() + duration
        while time.monotonic() < end:
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                out.append(raw.rstrip(b"\r").decode(errors="replace"))
            try:
                data = self.s.recv(4096)
                if not data:
                    break
                self.buf += data
            except socket.timeout:
                pass
        return out

    def expect(self, needle, timeout=4):
        end = time.monotonic() + timeout
        got = []
        while time.monotonic() < end:
            got += self.lines(0.2)
            if any(needle in line for line in got):
                return
        raise AssertionError(f"expected {needle!r}; got {got!r}")

    def disconnected(self, timeout=0.8):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            self.s.settimeout(max(0.05, end - time.monotonic()))
            try:
                data = self.s.recv(4096)
                if not data:
                    self.s.settimeout(0.2)
                    return True
            except socket.timeout:
                self.s.settimeout(0.2)
                return False
            except (ConnectionResetError, BrokenPipeError, OSError):
                self.s.settimeout(0.2)
                return True
        self.s.settimeout(0.2)
        return False

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_registration_timeout.py scratchircd")
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-regtimeout-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w") as f:
            f.write(
                f"server_name = test.local\n"
                f"network_name = TestNet\n"
                f"bind_address = 127.0.0.1\n"
                f"port = {port}\n"
                f"max_clients = 20\n"
                f"registration_timeout_seconds = 5\n"
                f"dns_timeout_seconds = 1\n"
                f"operators_db = {td}/operators.db\n"
                f"bans_db = {td}/bans.db\n"
                f"nickserv_db = {td}/nickserv.db\n"
                f"chanserv_db = {td}/chanserv.db\n"
                f"memoserv_db = {td}/memoserv.db\n"
                f"history_db = {td}/history.db\n"
                f"geoip_city_db = \n"
                f"geoip_asn_db = \n"
            )
        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        idle = cap = good = None
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    raise RuntimeError(proc.stderr.read())
                try:
                    probe = socket.create_connection(("127.0.0.1", port), timeout=0.1)
                    probe.close()
                    break
                except OSError:
                    time.sleep(0.05)

            idle = IRC(port)
            cap = IRC(port)
            cap.send("CAP LS 302")
            cap.expect(" CAP * LS :")

            good = IRC(port)
            good.send("NICK Good")
            good.send("USER good 0 * :Good User")
            good.expect(" 001 Good ")

            time.sleep(6.0)

            assert idle.disconnected(), "completely idle unregistered socket survived registration timeout"
            assert cap.disconnected(), "CAP-stalled unregistered socket survived registration timeout"

            good.send("PING :still-here")
            good.expect("PONG", 2)
            assert not good.disconnected(0.2), "registered client was incorrectly disconnected by registration timeout"
        finally:
            for client in (idle, cap, good):
                if client is not None:
                    client.close()
            if proc.poll() is None:
                proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

if __name__ == "__main__":
    main()
