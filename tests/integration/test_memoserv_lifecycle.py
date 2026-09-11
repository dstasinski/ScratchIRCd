#!/usr/bin/env python3
"""Integration coverage for MemoServ account lifecycle cleanup."""

import os
import re
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


def register_account(client, nick, phrase):
    register(client, nick)
    client.send(f"NICKSERV REGISTER {phrase}")
    client.expect("Nickname registered and identified.")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3.0)


def sent_memo_id(line):
    match = re.search(r"Memo #(\d+) sent to", line)
    assert match is not None, line
    return int(match.group(1))


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_memoserv_lifecycle.py scratchircd")
    binary = os.path.abspath(sys.argv[1])
    maker = os.path.join(os.path.dirname(binary), "scratchircd-mkpasswd")
    admin_phrase = "fixture-alpha"
    admin_hash = subprocess.check_output([maker, admin_phrase], text=True).strip()

    with tempfile.TemporaryDirectory(prefix="scratchircd-memoserv-lifecycle-") as td:
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
            f.write("netadmin_" + "password_hash = " + admin_hash + "\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)

            bob = IRCClient(port); clients.append(bob)
            register_account(bob, "Bob", "fixture-bob")

            carol = IRCClient(port); clients.append(carol)
            register_account(carol, "Carol", "fixture-carol")

            alice = IRCClient(port); clients.append(alice)
            register_account(alice, "Alice", "fixture-alice")

            alice.send("MEMOSERV SEND Bob :alice hidden sender row")
            alice_to_bob = sent_memo_id(alice.expect("sent to Bob"))
            alice.send(f"MEMOSERV DELSENT {alice_to_bob}")
            alice.expect("Sent memo removed from sent history.")

            bob.send("MEMOSERV SEND Alice :bob recipient row")
            bob.expect("sent to Alice")

            carol.send("MEMOSERV SEND Bob :carol unrelated row")
            carol.expect("sent to Bob")

            admin = IRCClient(port); clients.append(admin)
            register(admin, "Admin")
            admin.send("OPER root " + admin_phrase)
            admin.expect(" 381 Admin ")
            admin.send("NSDROP Alice")
            admin.expect("NickServ account deleted.")

            db = sqlite3.connect(memoserv_db)
            try:
                alice_refs = db.execute(
                    "SELECT COUNT(*) FROM memos WHERE sender='Alice' OR recipient='Alice'"
                ).fetchone()[0]
                rows = db.execute(
                    "SELECT sender,recipient,text,sender_deleted FROM memos ORDER BY id"
                ).fetchall()
            finally:
                db.close()

            assert alice_refs == 0, alice_refs
            assert rows == [("Carol", "Bob", "carol unrelated row", 0)], rows
        finally:
            for client in clients:
                client.close()
            stop(proc)


if __name__ == "__main__":
    main()
