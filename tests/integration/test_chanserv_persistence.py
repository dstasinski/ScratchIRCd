#!/usr/bin/env python3
"""End-to-end coverage for ChanServ MLOCK and parameter/list persistence."""

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
            sock = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            sock.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("listener did not start")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick}")
    client.expect(f" 001 {nick} ")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3.0)


def rfc1459_fold(text):
    table = str.maketrans({"{": "[", "}": "]", "|": "\\", "~": "^"})
    return text.lower().translate(table)


def rfc1459_collate(left, right):
    left_folded = rfc1459_fold(left)
    right_folded = rfc1459_fold(right)
    return (left_folded > right_folded) - (left_folded < right_folded)


def mask_rows(path):
    con = sqlite3.connect(path)
    try:
        # chanserv.db declares channel keys with the server's custom IRCNOCASE
        # collation. Standalone SQLite clients must register it before querying
        # tables whose schema/foreign keys refer to that collation.
        con.create_collation("IRCNOCASE", rfc1459_collate)
        return con.execute(
            "SELECT type,mask,protected_authorized FROM channel_masks "
            "WHERE channel=? ORDER BY type,mask", ("#persist",)
        ).fetchall()
    finally:
        con.close()


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_chanserv_persistence.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-cspersist-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        chanserv_db = os.path.join(td, "chanserv.db")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\n")
            f.write(f"chanserv_db = {chanserv_db}\n")
            f.write(f"history_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        alice = None
        try:
            wait_listen(port, proc)
            alice = IRCClient(port)
            register(alice, "Alice")
            alice.send("NICKSERV REGISTER chanpass")
            alice.expect("Nickname registered and identified.")
            alice.send("JOIN #persist")
            alice.expect(" JOIN #persist")
            alice.send("CHANSERV REGISTER #persist :Persistence test")
            alice.expect("Channel registered successfully.")
            alice.send("CHANSERV SET #persist MLOCK +nt")
            alice.expect("Persistent mode lock updated.")

            alice.send("MODE #persist +m")
            alice.expect(" 974 Alice m :Mode is locked by ChanServ")
            alice.send("MODE #persist -n")
            alice.expect(" 974 Alice n :Mode is locked by ChanServ")

            alice.send("MODE #persist +kljL secret 5 2:60 #overflow")
            alice.expect(" MODE #persist +kljL secret 5 2:60 #overflow")
            alice.send("MODE #persist +B #banned")
            alice.expect(" MODE #persist +B #banned")
            alice.send("MODE #persist +b Bad!*@*")
            alice.expect(" MODE #persist +b Bad!*@*")
            alice.send("MODE #persist +e Friend!*@*")
            alice.expect(" MODE #persist +e Friend!*@*")
            alice.send("MODE #persist +I Invite!*@*")
            alice.expect(" MODE #persist +I Invite!*@*")

            # Diagnose the live MODE layer before persistence/restart. If this
            # fails, +I was never added to Channel.invite_exception_list.
            alice.send("MODE #persist I")
            alice.expect(" Invite!*@* ")

            # Diagnose the persistence boundary before shutdown. Type 3 is
            # CHANSERV_MASK_INVEX; the exact row must already be in SQLite.
            rows = mask_rows(chanserv_db)
            assert (3, "Invite!*@*", 0) in rows, f"missing invex DB row; rows={rows!r}"
        finally:
            if alice is not None:
                alice.close()
            stop(proc)

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        traveler = None
        try:
            wait_listen(port, proc)
            traveler = IRCClient(port)
            register(traveler, "Traveler")
            traveler.send("IDENTIFY Alice chanpass")
            traveler.expect("Password accepted - you are now identified.")
            traveler.send("JOIN #persist secret")
            traveler.expect(" 366 Traveler #persist ")

            traveler.send("MODE #persist")
            lines = traveler.expect(" 324 Traveler #persist ")
            mode_line = next(line for line in lines if " 324 Traveler #persist " in line)
            for letter in ("n", "t", "r", "k", "l", "j", "L", "B"):
                assert letter in mode_line, mode_line
            assert "secret" in mode_line and "5" in mode_line and "2:60" in mode_line, mode_line
            assert "#overflow" in mode_line and "#banned" in mode_line, mode_line

            traveler.send("MODE #persist b")
            traveler.expect(" Bad!*@* ")
            traveler.send("MODE #persist e")
            traveler.expect(" Friend!*@* ")
            traveler.send("MODE #persist I")
            traveler.expect(" Invite!*@* ")

            traveler.send("MODE #persist +m")
            traveler.expect(" 974 Traveler m :Mode is locked by ChanServ")
        finally:
            if traveler is not None:
                traveler.close()
            stop(proc)


if __name__ == "__main__":
    main()
