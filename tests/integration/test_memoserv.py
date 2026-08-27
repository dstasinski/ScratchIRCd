#!/usr/bin/env python3
"""End-to-end coverage for virtual MemoServ persistence and restart lifecycle."""

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
    def send(self, line): self.sock.sendall((line + "\r\n").encode())
    def expect(self, needle, duration=5.0):
        deadline = time.monotonic() + duration; got = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode(errors="replace"); got.append(line)
                if needle in line: return line
            try:
                data = self.sock.recv(4096)
                if not data: break
                self.buffer += data
            except socket.timeout: pass
        raise AssertionError(f"expected {needle!r}; got {got!r}")
    def close(self):
        try: self.sock.close()
        except OSError: pass

def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

def wait_listen(port, proc):
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if proc.poll() is not None: raise RuntimeError(proc.stderr.read())
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.1); s.close(); return
        except OSError: time.sleep(0.05)
    raise RuntimeError("listener did not start")

def register(c, nick):
    c.send(f"NICK {nick}"); c.send(f"USER {nick} 0 * :{nick}"); c.expect(f" 001 {nick} ")

def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try: proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired: proc.kill(); proc.wait(timeout=3.0)

def main():
    if len(sys.argv) != 2: raise SystemExit("usage: test_memoserv.py scratchircd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.join(os.path.dirname(binary), "scratchircd-mkpasswd")
    admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
    with tempfile.TemporaryDirectory(prefix="scratchircd-memoserv-") as td:
        port = free_port(); conf = os.path.join(td, "ircd.conf")
        memoserv_db = os.path.join(td, "memoserv.db")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
                f.write(f"{name}_db = {td}/{name}.db\n")
            f.write("memoserv_quota = 2\nmemoserv_retention_days = 90\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        bob = alice = None
        try:
            wait_listen(port, proc)
            bob = IRCClient(port); register(bob, "Bob")
            bob.send("NICKSERV REGISTER bobpass"); bob.expect("Nickname registered and identified.")
            bob.send("QUIT :offline"); bob.close(); bob = None

            alice = IRCClient(port); register(alice, "Alice")
            alice.send("NICKSERV REGISTER alicepass"); alice.expect("Nickname registered and identified.")
            alice.send("MEMOSERV SEND Bob :First memo")
            first_line = alice.expect("sent to Bob"); first_id = int(first_line.split("#",1)[1].split(" ",1)[0])
            alice.send("MEMOSERV SEND Bob :Second memo"); alice.expect("sent to Bob")
            alice.send("MEMOSERV SEND Bob :Third memo"); alice.expect("Recipient memo box is full.")
            alice.send("MEMOSERV SENT"); alice.expect("TO Bob")
            alice.send("MEMOSERV STATUS"); alice.expect("Memos: 0/2 stored, 0 unread.")
        finally:
            if bob is not None: bob.close()
            if alice is not None: alice.close()
            stop(proc)

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        traveler = None
        try:
            wait_listen(port, proc)
            traveler = IRCClient(port); register(traveler, "Traveler")
            traveler.send("MEMOSERV STATUS"); traveler.expect("You must identify to NickServ")
            traveler.send("IDENTIFY Bob bobpass"); traveler.expect("Password accepted - you are now identified.")
            traveler.send("MEMOSERV STATUS"); traveler.expect("Memos: 2/2 stored, 2 unread.")
            traveler.send("MEMOSERV LIST"); traveler.expect("UNREAD from Alice")
            traveler.send(f"MEMOSERV READ {first_id}"); traveler.expect("First memo")
            traveler.send(f"MEMOSERV REPLY {first_id} :Reply to Alice"); traveler.expect("Reply memo #")
            traveler.send(f"MEMOSERV FORWARD {first_id} Alice"); traveler.expect("forwarded to Alice")
            traveler.send("MEMOSERV STATUS"); traveler.expect("Memos: 2/2 stored, 1 unread.")
            traveler.send("ISON MemoServ"); line = traveler.expect(" 303 Traveler :")
            assert "MemoServ" not in line.split(":", 2)[-1], line
        finally:
            if traveler is not None: traveler.close()
            stop(proc)

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        alice2 = alice3 = None
        try:
            wait_listen(port, proc)
            alice2 = IRCClient(port); register(alice2, "AliceAgain")
            alice2.send("IDENTIFY Alice alicepass"); alice2.expect("Password accepted - you are now identified.")
            alice2.send("MEMOSERV STATUS"); alice2.expect("Memos: 2/2 stored, 2 unread.")
            alice2.send("MEMOSERV LIST"); alice2.expect("UNREAD from Bob")

            # STATUS above warms the five-minute retention throttle. Insert an
            # already-expired memo directly into persistent storage; another
            # command during ordinary uptime must honor the throttle and leave
            # it alone until the maintenance interval elapses.
            db = sqlite3.connect(memoserv_db)
            try:
                expired = int(time.time()) - 91 * 86400
                db.execute(
                    "INSERT INTO memos(sender,recipient,text,created_at,read_at) "
                    "VALUES(?,?,?,?,0)",
                    ("OldSender", "Alice", "expired restart probe", expired),
                )
                db.commit()
            finally:
                db.close()

            alice2.send("MEMOSERV STATUS"); alice2.expect("Memos: 3/2 stored, 3 unread.")

            # An in-process RESTART must clear the process-local maintenance
            # throttle. The first MemoServ access afterward should therefore
            # purge the expired row immediately, matching a clean process start.
            alice2.send("OPER root adminpass")
            alice2.expect(" 381 AliceAgain :You are now a Network Administrator")
            alice2.send("RESTART")
            alice2.expect("NOTICE AliceAgain :Restarting ScratchIRCd")
            alice2.close(); alice2 = None

            time.sleep(0.2)
            wait_listen(port, proc)
            alice3 = IRCClient(port); register(alice3, "AliceAfterRestart")
            alice3.send("IDENTIFY Alice alicepass")
            alice3.expect("Password accepted - you are now identified.")
            alice3.send("MEMOSERV STATUS")
            alice3.expect("Memos: 2/2 stored, 2 unread.")

            db = sqlite3.connect(memoserv_db)
            try:
                remaining = db.execute(
                    "SELECT COUNT(*) FROM memos WHERE text='expired restart probe'"
                ).fetchone()[0]
            finally:
                db.close()
            assert remaining == 0, remaining
        finally:
            if alice2 is not None: alice2.close()
            if alice3 is not None: alice3.close()
            stop(proc)

if __name__ == "__main__": main()
