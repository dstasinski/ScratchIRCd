#!/usr/bin/env python3
"""Integration coverage for MemoServ netadmin inspection and purge commands."""

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
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {got!r}")

    def collect_for(self, duration=0.75):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                got.append(raw.rstrip(b"\r").decode(errors="replace"))
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            got.append(raw.rstrip(b"\r").decode(errors="replace"))
        return got

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
            sock = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            sock.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("listener did not start")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick[:10]} 0 * :{nick}")
    client.expect(f" 001 {nick} ")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3.0)


def assert_memoserv_notice(line, nick):
    prefix = f":MemoServ!service@test.local NOTICE {nick} :"
    assert line.startswith(prefix), line


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_memoserv_admin.py scratchircd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.join(os.path.dirname(binary), "scratchircd-mkpasswd")
    admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()

    with tempfile.TemporaryDirectory(prefix="scratchircd-memoserv-admin-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        memoserv_db = os.path.join(td, "memoserv.db")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
                f.write(f"{name}_db = {td}/{name}.db\n")
            f.write("memoserv_quota = 10\nmemoserv_retention_days = 90\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        secret_text = "private memo body must not leak"
        try:
            wait_listen(port, proc)

            bob = IRCClient(port); clients.append(bob)
            register(bob, "Bob")
            bob.send("NICKSERV REGISTER bobpass")
            bob.expect("Nickname registered and identified.")

            alice = IRCClient(port); clients.append(alice)
            register(alice, "Alice")
            alice.send("NICKSERV REGISTER alicepass")
            alice.expect("Nickname registered and identified.")
            alice.send(f"MEMOSERV SEND Bob :{secret_text}")
            alice.expect("sent to Bob")

            ordinary = IRCClient(port); clients.append(ordinary)
            register(ordinary, "Ordinary")
            ordinary.send("MSINFO Bob")
            ordinary.expect(" 481 Ordinary ")
            ordinary.send("MSPURGE Bob")
            ordinary.expect(" 481 Ordinary ")

            admin = IRCClient(port); clients.append(admin)
            register(admin, "Admin")
            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin ")
            admin.send("MSINFO Bob")
            info_line = admin.expect("MEMOSERV account=Bob stored=1 unread=1")
            assert_memoserv_notice(info_line, "Admin")
            assert secret_text not in info_line, info_line

            db = sqlite3.connect(memoserv_db)
            try:
                expired = int(time.time()) - 91 * 86400
                db.execute(
                    "INSERT INTO memos(sender,recipient,text,created_at,read_at) "
                    "VALUES(?,?,?,?,0)",
                    ("OldSender", "Bob", "expired admin purge probe", expired),
                )
                db.commit()
            finally:
                db.close()

            admin.send("MSINFO Bob")
            info_line = admin.expect("MEMOSERV account=Bob stored=2 unread=2")
            assert_memoserv_notice(info_line, "Admin")
            admin.send("MSPURGE Bob")
            purge_line = admin.expect("MemoServ purge deleted 1 expired memo.")
            assert_memoserv_notice(purge_line, "Admin")
            admin.send("MSINFO Bob")
            info_line = admin.expect("MEMOSERV account=Bob stored=1 unread=1")
            assert_memoserv_notice(info_line, "Admin")

            lines = admin.collect_for()
            assert not any("expired admin purge probe" in line for line in lines), lines
        finally:
            for client in clients:
                client.close()
            stop(proc)


if __name__ == "__main__":
    main()
