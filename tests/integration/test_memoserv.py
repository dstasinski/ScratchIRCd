#!/usr/bin/env python3
"""End-to-end coverage for virtual MemoServ persistence and restart lifecycle."""

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
    def collect_for(self, duration=1.0):
        deadline = time.monotonic() + duration; got = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                got.append(raw.rstrip(b"\r").decode(errors="replace"))
            try:
                data = self.sock.recv(4096)
                if not data: break
                self.buffer += data
            except socket.timeout: pass
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            got.append(raw.rstrip(b"\r").decode(errors="replace"))
        return got
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
    c.send(f"NICK {nick}"); c.send(f"USER {nick[:10]} 0 * :{nick}"); c.expect(f" 001 {nick} ")

def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try: proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired: proc.kill(); proc.wait(timeout=3.0)

def assert_utc_timestamp(text):
    assert re.search(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", text), text

def oper_as_netadmin(client, nick):
    deadline = time.monotonic() + 8.0
    last_lines = []
    while time.monotonic() < deadline:
        client.send("OPER root adminpass")
        lines = client.collect_for(1.0)
        last_lines = lines
        if any(f" 381 {nick} :You are now a Network Administrator" in line for line in lines):
            return
        if any(f" 263 {nick} OPER " in line for line in lines):
            time.sleep(1.0)
            continue
        break
    raise AssertionError(f"expected OPER success for {nick!r}; got {last_lines!r}")

def memoserv_lines(client, command, nick, duration=1.0):
    deadline = time.monotonic() + 8.0
    last_lines = []
    while time.monotonic() < deadline:
        client.send(command)
        lines = client.collect_for(duration)
        last_lines = lines
        if any(f" 263 {nick} MEMOSERV " in line for line in lines):
            time.sleep(1.0)
            continue
        return lines
    raise AssertionError(f"MEMOSERV throttled too long for {command!r}; got {last_lines!r}")

def main():
    if len(sys.argv) != 2: raise SystemExit("usage: test_memoserv.py scratchircd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.join(os.path.dirname(binary), "scratchircd-mkpasswd")
    admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
    with tempfile.TemporaryDirectory(prefix="scratchircd-memoserv-") as td:
        port = free_port(); conf = os.path.join(td, "ircd.conf")
        memoserv_db = os.path.join(td, "memoserv.db")
        server_name = "s" * 63
        with open(conf, "w", encoding="utf-8") as f:
            f.write(f"server_name = {server_name}\nnetwork_name = TestNet\n")
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
        long_text = "X" * 400
        try:
            wait_listen(port, proc)
            bob = IRCClient(port); register(bob, "Bob")
            bob.send("NICKSERV REGISTER bobpass"); bob.expect("Nickname registered and identified.")
            bob.send("QUIT :offline"); bob.close(); bob = None

            alice = IRCClient(port); register(alice, "Alice")
            alice.send("NICKSERV REGISTER alicepass"); alice.expect("Nickname registered and identified.")
            alice.send("MEMOSERV SEND Bob :First memo")
            first_line = alice.expect("sent to Bob"); first_id = int(first_line.split("#",1)[1].split(" ",1)[0])
            alice.send(f"MEMOSERV SEND Bob :{long_text}")
            second_line = alice.expect("sent to Bob"); second_id = int(second_line.split("#",1)[1].split(" ",1)[0])
            alice.send("MEMOSERV SEND Bob :Third memo"); alice.expect("Recipient memo box is full.")
            alice.send("MEMOSERV SENT")
            sent_unread_line = alice.expect("TO Bob UNREAD sent ")
            assert " read " not in sent_unread_line, sent_unread_line
            assert_utc_timestamp(sent_unread_line)
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

            # A maximum-size persisted memo must be delivered completely even
            # when MemoServ metadata makes a single NOTICE exceed 510 bytes.
            traveler.send(f"MEMOSERV READ {second_id}")
            read_lines = traveler.collect_for(1.0)
            prefix = f":MemoServ!service@{server_name} NOTICE Traveler :"
            payloads = [line[len(prefix):] for line in read_lines if line.startswith(prefix)]
            assert len(payloads) >= 2, read_lines
            assert long_text in "".join(payloads), payloads
            assert all(len(line.encode()) <= 510 for line in read_lines), read_lines

            traveler.send(f"MEMOSERV REPLY {first_id} :Reply to Alice"); traveler.expect("Reply memo #")
            traveler.send(f"MEMOSERV FORWARD {first_id} Alice"); traveler.expect("forwarded to Alice")
            traveler.send("MEMOSERV STATUS"); traveler.expect("Memos: 2/2 stored, 0 unread.")

            # Recipient deletion hides only Bob's inbox side. Alice must still
            # be able to see the original memo in SENT until she uses DELSENT.
            traveler.send(f"MEMOSERV DEL {first_id}")
            traveler.expect("Memo deleted.")
            traveler.send(f"MEMOSERV READ {first_id}")
            traveler.expect("No such memo.")
            traveler.send("MEMOSERV STATUS")
            traveler.expect("Memos: 1/2 stored, 0 unread.")
            list_lines = memoserv_lines(traveler, "MEMOSERV LIST", "Traveler")
            assert not any(f"#{first_id} " in line for line in list_lines), list_lines
            assert any(f"#{second_id} READ from Alice" in line for line in list_lines), list_lines
            db = sqlite3.connect(memoserv_db)
            try:
                first_state = db.execute(
                    "SELECT sender_deleted,recipient_deleted FROM memos WHERE id=?",
                    (first_id,),
                ).fetchone()
            finally:
                db.close()
            assert first_state == (0, 1), first_state

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
            alice2.send("MEMOSERV SENT")
            sent_read_line = alice2.expect("TO Bob READ sent ")
            assert " read " in sent_read_line, sent_read_line
            assert len(re.findall(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", sent_read_line)) == 2, sent_read_line

            alice2.send(f"MEMOSERV DELSENT {first_id}")
            alice2.expect("Sent memo removed from sent history.")
            sent_lines = memoserv_lines(alice2, "MEMOSERV SENT", "AliceAgain")
            assert not any(f"#{first_id} TO Bob" in line for line in sent_lines), sent_lines
            assert any(f"#{second_id} TO Bob READ sent " in line for line in sent_lines), sent_lines

            alice2.send("MEMOSERV DELSENT ALL")
            alice2.expect("All sent memos removed from sent history.")
            alice2.send("MEMOSERV SENT")
            alice2.expect("You have no sent memos.")
            alice2.send("MEMOSERV STATUS")
            alice2.expect("Memos: 2/2 stored, 2 unread.")

            # DELSENT hides sender history without deleting the recipient's
            # copy. The first Alice->Bob row was already hidden by Bob, so it
            # is purged when Alice hides it too; the second remains for Bob.
            db = sqlite3.connect(memoserv_db)
            try:
                bob_rows = db.execute(
                    "SELECT COUNT(*) FROM memos WHERE recipient='Bob' AND sender='Alice'"
                ).fetchone()[0]
                hidden_rows = db.execute(
                    "SELECT COUNT(*) FROM memos WHERE recipient='Bob' AND sender='Alice' "
                    "AND sender_deleted=1"
                ).fetchone()[0]
            finally:
                db.close()
            assert bob_rows == 1, bob_rows
            assert hidden_rows == 1, hidden_rows

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
            oper_as_netadmin(alice2, "AliceAgain")
            alice2.send("RESTART")
            alice2.expect("NOTICE AliceAgain :Restarting ScratchIRCd")
            alice2.close(); alice2 = None

            time.sleep(0.2)
            wait_listen(port, proc)
            alice3 = IRCClient(port); register(alice3, "AliceAfterStart")
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
