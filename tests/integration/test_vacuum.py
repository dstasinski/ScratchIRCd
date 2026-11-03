#!/usr/bin/env python3
"""End-to-end VACUUM authorization, integrity, selection, and status checks."""
import os
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time

def irc_fold(value):
    return value.lower().translate(str.maketrans({"{": "[", "}": "]", "|": "\\", "~": "^"}))

def irc_nocase(left, right):
    a, b = irc_fold(left), irc_fold(right)
    return (a > b) - (a < b)

def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

class Peer:
    def __init__(self, port, nick):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.sock.settimeout(.15)
        self.buffer = b""
        self.nick = nick
        self.send("NICK " + nick)
        self.send("USER " + nick + " 0 * :" + nick)
        self.expect(" 001 " + nick + " ")
    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())
    def expect(self, needle, timeout=6):
        deadline = time.monotonic() + timeout
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
                if data:
                    self.buffer += data
                else:
                    break
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}, got {seen!r}")
    def close(self):
        self.sock.close()

def main():
    binary, mkpasswd = map(os.path.abspath, sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="scratch-vacuum-") as td:
        port = free_port()
        password_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        conf = os.path.join(td, "ircd.conf")
        names = ("operators", "bans", "nickserv", "chanserv", "memoserv", "history")
        with open(conf, "w", encoding="utf-8") as out:
            out.write(f"server_name = test.local\nnetwork_name = TestNet\nbind_address = 127.0.0.1\nport = {port}\n")
            out.write("max_clients = 16\nnospoof = no\ndns_timeout_seconds = 1\ngeoip_city_db = \ngeoip_asn_db = \n")
            for name in names:
                out.write(f"{name}_db = {td}/{name}.db\n")
            out.write(f"netadmin_name = root\nnetadmin_password_hash = {password_hash}\nnetadmin_hostmask = *!*@127.0.0.1\n")
        proc = subprocess.Popen([binary, conf], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        ordinary = admin = None
        try:
            deadline = time.monotonic() + 6
            while True:
                if proc.poll() is not None:
                    raise RuntimeError(proc.stderr.read())
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=.1):
                        break
                except OSError:
                    if time.monotonic() > deadline:
                        raise RuntimeError("server did not start")
                    time.sleep(.05)
            ordinary = Peer(port, "Ordinary")
            admin = Peer(port, "Admin")
            for cmd in ("LIST", "STATUS", "ALL", "NICKSERV"):
                ordinary.send("VACUUM " + cmd)
                ordinary.expect(" 481 Ordinary ")
            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin ")
            admin.send("VACUUM LIST")
            admin.expect("VACUUM NICKSERV:")
            admin.send("VACUUM STATUS")
            admin.expect("VACUUM idle")
            admin.send("VACUUM INVALID")
            admin.expect("Syntax:")
            path = os.path.join(td, "nickserv.db")
            with sqlite3.connect(path) as db:
                db.execute("CREATE TABLE IF NOT EXISTS vacuum_probe (id INTEGER PRIMARY KEY, text TEXT)")
                db.execute("INSERT INTO vacuum_probe(text) VALUES ('preserved')")
                db.commit()
            admin.send("VACUUM NICKSERV")
            admin.expect("VACUUM started")
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                admin.send("VACUUM STATUS")
                status = admin.expect("NOTICE Admin :VACUUM ", 2)
                if "idle" in status and "completed=1" in status:
                    assert "failed=0" in status, status
                    break
                time.sleep(.1)
            else:
                raise AssertionError("NICKSERV VACUUM did not finish")
            with sqlite3.connect(path) as db:
                assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
                assert db.execute("SELECT text FROM vacuum_probe").fetchone()[0] == "preserved"
            # Some optional databases are created lazily by the server.
            # Ensure ALL operates on six existing, valid SQLite files.
            for name in names:
                db_path = os.path.join(td, name + ".db")
                with sqlite3.connect(db_path) as db:
                    db.execute("CREATE TABLE IF NOT EXISTS vacuum_test_marker (id INTEGER PRIMARY KEY)")
                    db.commit()
            admin.send("VACUUM ALL")
            admin.expect("VACUUM started")
            deadline = time.monotonic() + 15
            last_status = "(no response)"
            while time.monotonic() < deadline:
                admin.send("VACUUM STATUS")
                status = admin.expect("NOTICE Admin :VACUUM ", 2)
                last_status = status
                if "idle" in status and "completed=6" in status:
                    assert "failed=0" in status, status
                    break
                if "idle" in status and "failed=" in status and "failed=0" not in status:
                    raise AssertionError("ALL VACUUM failed: " + status)
                time.sleep(.1)
            else:
                raise AssertionError("ALL VACUUM did not finish; last status: " + last_status)
            for name in names:
                with sqlite3.connect(os.path.join(td, name + ".db")) as db:
                    if name == "chanserv":
                        db.create_collation("IRCNOCASE", irc_nocase)
                    assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
            # An exclusive SQLite lock must fail safely without modifying data.
            lock = sqlite3.connect(path, timeout=1, isolation_level=None)
            try:
                lock.execute("BEGIN EXCLUSIVE")
                admin.send("VACUUM NICKSERV")
                admin.expect("VACUUM started")
                deadline = time.monotonic() + 8
                last_status = ""
                while time.monotonic() < deadline:
                    admin.send("VACUUM STATUS")
                    last_status = admin.expect("NOTICE Admin :VACUUM ", 2)
                    if "idle" in last_status and "failed=1" in last_status:
                        assert "locked" in last_status or "busy" in last_status, last_status
                        break
                    time.sleep(.1)
                else:
                    raise AssertionError("locked VACUUM did not report failure: " + last_status)
            finally:
                lock.execute("ROLLBACK")
                lock.close()
            with sqlite3.connect(path) as db:
                assert db.execute("SELECT text FROM vacuum_probe").fetchone()[0] == "preserved"

            # Shutdown while a VACUUM is blocked by another connection.
            # The worker must not keep the daemon alive indefinitely.
            lock = sqlite3.connect(path, timeout=1, isolation_level=None)
            try:
                lock.execute("BEGIN EXCLUSIVE")
                admin.send("VACUUM NICKSERV")
                admin.expect("VACUUM started")
                admin.send("DIE")
                admin.expect("Shutting down ScratchIRCd")
                started = time.monotonic()
                assert proc.wait(timeout=5) == 0
                assert time.monotonic() - started < 5, "VACUUM delayed shutdown"
            finally:
                lock.execute("ROLLBACK")
                lock.close()
            with sqlite3.connect(path) as db:
                assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
                assert db.execute("SELECT text FROM vacuum_probe").fetchone()[0] == "preserved"
        finally:
            if ordinary: ordinary.close()
            if admin: admin.close()
            if proc.poll() is None:
                proc.terminate()
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)

if __name__ == "__main__":
    main()
