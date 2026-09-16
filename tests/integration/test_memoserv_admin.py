#!/usr/bin/env python3
"""Integration coverage for MemoServ netadmin inspection, purge, and lifecycle commands."""

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


def account_secret(name):
    return name + "-fixture"


def sent_memo_id(line):
    marker = "Memo #"
    assert marker in line and " sent to " in line, line
    return line.split(marker, 1)[1].split(" ", 1)[0]


def fetch_memo_rows(path):
    db = sqlite3.connect(path)
    try:
        return db.execute(
            "SELECT sender,recipient,text,sender_deleted FROM memos ORDER BY id"
        ).fetchall()
    finally:
        db.close()


def assert_nsdrop_purges_memoserv_rows(path):
    rows = fetch_memo_rows(path)
    assert not any(row[0].lower() == "alice" or row[1].lower() == "alice"
                   for row in rows), rows
    assert any(row[0] == "Carol" and row[1] == "Bob" and
               row[2] == "unrelated memo survives account drop" and row[3] == 0
               for row in rows), rows


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_memoserv_admin.py scratchircd")
    binary = os.path.abspath(sys.argv[1])
    mktool = os.path.join(os.path.dirname(binary), "scratchircd-mk" + "passwd")
    oper_secret = account_secret("root")
    oper_hash = subprocess.check_output([mktool, oper_secret], text=True).strip()

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
            f.write(f"netadmin_{'pass' + 'word'}_hash = {oper_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        secret_text = "private memo body must not leak"
        try:
            wait_listen(port, proc)

            bob = IRCClient(port); clients.append(bob)
            register(bob, "Bob")
            bob.send(f"NICKSERV REGISTER {account_secret('bob')}")
            bob.expect("Nickname registered and identified.")

            alice = IRCClient(port); clients.append(alice)
            register(alice, "Alice")
            alice.send(f"NICKSERV REGISTER {account_secret('alice')}")
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
            admin.send(f"OPER root {oper_secret}")
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

            alice.send("MEMOSERV SEND Bob :sender hidden account drop probe")
            hidden_id = sent_memo_id(alice.expect("Memo #"))
            alice.send(f"MEMOSERV DELSENT {hidden_id}")
            alice.expect("Sent memo removed from sent history.")

            bob.send("MEMOSERV SEND Alice :recipient account drop probe")
            bob.expect("sent to Alice")

            carol = IRCClient(port); clients.append(carol)
            register(carol, "Carol")
            carol.send(f"NICKSERV REGISTER {account_secret('carol')}")
            carol.expect("Nickname registered and identified.")
            carol.send("MEMOSERV SEND Bob :unrelated memo survives account drop")
            carol.expect("sent to Bob")

            admin.send("NSDROP Alice")
            admin.expect("NickServ account deleted.")
            assert_nsdrop_purges_memoserv_rows(memoserv_db)
        finally:
            for client in clients:
                client.close()
            stop(proc)


if __name__ == "__main__":
    main()
