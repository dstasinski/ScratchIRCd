#!/usr/bin/env python3
"""End-to-end tests for oper-controlled user modes +D and +M."""

import os
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time


class IRCClient:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
        self.sock.settimeout(0.2)
        self.buffer = b""
        self.pending = []

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def _next(self, deadline):
        if self.pending:
            return self.pending.pop(0)
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                return raw.rstrip(b"\r").decode(errors="replace")
            try:
                data = self.sock.recv(4096)
                if not data:
                    return None
                self.buffer += data
            except socket.timeout:
                pass
        return None

    def expect(self, needle, duration=3.0):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            line = self._next(deadline)
            if line is None:
                continue
            got.append(line)
            if needle in line:
                return line
        raise AssertionError(f"expected {needle!r}; got {got!r}")

    def expect_not(self, needle, duration=0.5):
        deadline = time.monotonic() + duration
        kept = []
        while time.monotonic() < deadline:
            line = self._next(deadline)
            if line is None:
                continue
            if needle in line:
                raise AssertionError(f"unexpected {needle!r}: {line!r}")
            kept.append(line)
        self.pending.extend(kept)

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
    raise RuntimeError("server did not listen")


def register(c, nick):
    c.send(f"NICK {nick}")
    c.send(f"USER {nick} 0 * :{nick}")
    c.expect(f" 001 {nick} ")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait(timeout=3)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_moderation.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-moderation-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        bans_db = os.path.join(td, "bans.db")
        admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\nbans_db = {bans_db}\n")
            f.write(f"nickserv_db = {td}/nickserv.db\nchanserv_db = {td}/chanserv.db\n")
            f.write(f"memoserv_db = {td}/memoserv.db\nhistory_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)
            admin = IRCClient(port); clients.append(admin); register(admin, "Admin")
            bob = IRCClient(port); clients.append(bob); register(bob, "Bob")
            carol = IRCClient(port); clients.append(carol); register(carol, "Carol")
            dave = IRCClient(port); clients.append(dave); register(dave, "Dave")
            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin :You are now a Network Administrator")

            # Ordinary users cannot control +D/+M by command or MODE.
            carol.send("DEAF +Bob"); carol.expect(" 481 Carol ")
            bob.send("MODE Bob +D"); bob.expect(" 481 Bob ")
            carol.send("MUTE +Bob"); carol.expect(" 481 Carol ")
            bob.send("MODE Bob +M"); bob.expect(" 481 Bob ")

            # +D blocks private traffic both ways except with opers.
            admin.send("DEAF +Bob"); bob.expect(" MODE Bob +D")
            bob.send("MODE Bob"); bob.expect(" 221 Bob +D")
            carol.send("PRIVMSG Bob :hello")
            carol.expect("NOTICE Carol :I cannot send or receive private messages.")
            bob.expect_not("PRIVMSG Bob :hello")
            carol.send("NOTICE Bob :notice")
            bob.expect_not("NOTICE Bob :notice")
            bob.send("PRIVMSG Carol :blocked outgoing")
            carol.expect_not("blocked outgoing")
            bob.send("NOTICE Carol :blocked outgoing notice")
            carol.expect_not("blocked outgoing notice")

            admin.send("PRIVMSG Bob :oper inbound")
            bob.expect("PRIVMSG Bob :oper inbound")
            bob.send("PRIVMSG Admin :oper outbound")
            admin.expect("PRIVMSG Admin :oper outbound")

            admin.send("DEAF -Bob"); bob.expect(" MODE Bob -D")
            carol.send("PRIVMSG Bob :restored")
            bob.expect("PRIVMSG Bob :restored")

            # Admin creates the channel, so Bob joins as an ordinary member.
            admin.send("JOIN #mute"); admin.expect(" 366 Admin #mute ")
            bob.send("JOIN #mute"); bob.expect(" 366 Bob #mute ")
            carol.send("JOIN #mute"); carol.expect(" 366 Carol #mute ")
            dave.send("JOIN #mute"); dave.expect(" 366 Dave #mute ")
            admin.send("MUTE +Bob"); bob.expect(" MODE Bob +M")
            bob.send("MODE Bob"); bob.expect(" 221 Bob +M")

            # +M blocks only an ordinary/unprivileged channel member.
            bob.send("PRIVMSG #mute :blocked channel text")
            bob.expect(" 404 Bob #mute ")
            carol.expect_not("blocked channel text")
            bob.send("NOTICE #mute :blocked channel notice")
            bob.expect(" 404 Bob #mute ")
            carol.expect_not("blocked channel notice")

            # Any membership privilege makes +M ineffective in that channel.
            for mode, label in (("v", "voice"), ("h", "halfop"), ("o", "op"),
                                ("a", "protected"), ("q", "owner")):
                admin.send(f"MODE #mute +{mode} Bob")
                bob.expect(f" MODE #mute +{mode} Bob")
                bob.send(f"PRIVMSG #mute :{label} immune")
                carol.expect(f"PRIVMSG #mute :{label} immune")
                admin.send(f"MODE #mute -{mode} Bob")
                bob.expect(f" MODE #mute -{mode} Bob")
                bob.send(f"PRIVMSG #mute :{label} removed")
                bob.expect(" 404 Bob #mute ")
                carol.expect_not(f"{label} removed")

            # IRCops/netadmins are globally immune to +M even without a
            # channel membership privilege.
            admin.send("MODE #mute -q Admin")
            admin.send("MUTE +Admin"); admin.expect(" MODE Admin +M")
            admin.send("PRIVMSG #mute :oper immune")
            carol.expect("PRIVMSG #mute :oper immune")
            admin.send("MUTE -Admin"); admin.expect(" MODE Admin -M")

            admin.send("MUTE -Bob"); bob.expect(" MODE Bob -M")
            bob.send("PRIVMSG #mute :channel restored")
            carol.expect("PRIVMSG #mute :channel restored")

            # A malformed persistent policy row must never be truncated into a
            # different KLINE or treated as "no ban". Inject an overlong mask
            # directly to simulate legacy/manual SQLite corruption. Registration
            # must fail closed through the real policy lookup path.
            corrupt_mask = "*" * 256
            db = sqlite3.connect(bans_db)
            try:
                db.execute(
                    "INSERT INTO bans(type,mask,reason,set_by,created_at,expires_at) "
                    "VALUES(1,?,?,?,unixepoch(),0)",
                    (corrupt_mask, "corrupt policy", "root"),
                )
                db.commit()
            finally:
                db.close()

            corrupt = IRCClient(port); clients.append(corrupt)
            corrupt.send("NICK Corrupt")
            corrupt.send("USER corrupt 0 * :Corrupt")
            corrupt.expect(" 465 Corrupt ")
            corrupt.expect_not(" 001 Corrupt ", duration=0.3)

            db = sqlite3.connect(bans_db)
            try:
                db.execute("DELETE FROM bans WHERE type=1 AND mask=?", (corrupt_mask,))
                db.commit()
            finally:
                db.close()
        finally:
            for c in clients: c.close()
            stop(proc)


if __name__ == "__main__":
    main()
