#!/usr/bin/env python3
"""End-to-end coverage for behavioral user modes +d, +W, and +x."""

import os
import socket
import subprocess
import sys
import tempfile
import time


class IRCClient:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
        self.sock.settimeout(0.20)
        self.buffer = b""
        self.pending = []

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def _pump(self):
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            self.pending.append(raw.rstrip(b"\r").decode(errors="replace"))

    def expect(self, needle, duration=4.0):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
            self._pump()
            for i, line in enumerate(self.pending):
                if needle in line:
                    seen.extend(self.pending[:i + 1])
                    del self.pending[:i + 1]
                    return line
            seen.extend(self.pending)
            self.pending.clear()
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {seen!r}")

    def expect_not(self, needle, duration=0.6):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
            self._pump()
            for line in self.pending:
                if needle in line:
                    raise AssertionError(f"unexpected {needle!r}; got {seen + self.pending!r}")
            seen.extend(self.pending)
            self.pending.clear()
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
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


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick.lower()} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_user_modes.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-user-modes-") as td:
        port = free_port()
        admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\n")
            f.write(f"chanserv_db = {td}/chanserv.db\n")
            f.write(f"memoserv_db = {td}/memoserv.db\n")
            f.write(f"history_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)
            admin = IRCClient(port); clients.append(admin)
            deaf = IRCClient(port); clients.append(deaf)
            speaker = IRCClient(port); clients.append(speaker)
            register(admin, "Admin")
            register(deaf, "Deaf")
            register(speaker, "Speaker")

            deaf.send("JOIN #modes"); deaf.expect(" JOIN #modes")
            speaker.send("JOIN #modes"); speaker.expect(" JOIN #modes")
            deaf.send("MODE Deaf +d"); deaf.expect(" 221 Deaf +d")

            speaker.send("PRIVMSG #modes :ordinary chatter")
            deaf.expect_not("ordinary chatter")
            speaker.send("PRIVMSG #modes :!bot command")
            deaf.expect("PRIVMSG #modes :!bot command")
            # +d affects channel PRIVMSG only, not NOTICE.
            speaker.send("NOTICE #modes :channel notice")
            deaf.expect("NOTICE #modes :channel notice")

            # +x changes only the publicly displayed hostname.
            deaf.send("MODE Deaf +x")
            deaf.expect(" 221 Deaf +dx")
            speaker.send("WHOIS Deaf")
            whois = speaker.expect(" 311 Speaker Deaf ")
            assert "cloak-" in whois, whois
            assert "127.0.0.1" not in whois, whois
            deaf.send("MODE Deaf -x")
            deaf.expect(" 221 Deaf +d")
            speaker.send("WHOIS Deaf")
            whois = speaker.expect(" 311 Speaker Deaf ")
            assert "cloak-" not in whois, whois

            # +W is IRCop-only.
            deaf.send("MODE Deaf +W")
            deaf.expect(" 481 Deaf ")
            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin :You are now a Network Administrator")
            admin.send("MODE Admin +W")
            admin.expect(" 221 Admin ")
            speaker.send("WHOIS Admin")
            admin.expect("did a /WHOIS on you")
        finally:
            for client in clients:
                client.close()
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3.0)
                except subprocess.TimeoutExpired:
                    proc.kill(); proc.wait(timeout=3.0)


if __name__ == "__main__":
    main()
