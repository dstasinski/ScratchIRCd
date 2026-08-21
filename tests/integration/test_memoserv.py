#!/usr/bin/env python3
"""End-to-end coverage for virtual MemoServ and persistent offline memos."""

import os
import socket
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
            s = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            s.close()
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


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_memoserv.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-memoserv-") as td:
        port = free_port()
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

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        bob = alice = None
        try:
            wait_listen(port, proc)

            bob = IRCClient(port)
            register(bob, "Bob")
            bob.send("NICKSERV REGISTER bobpass")
            bob.expect("Nickname registered and identified.")
            bob.send("QUIT :offline")
            bob.close()
            bob = None

            alice = IRCClient(port)
            register(alice, "Alice")
            alice.send("NICKSERV REGISTER alicepass")
            alice.expect("Nickname registered and identified.")
            alice.send("MEMOSERV SEND Bob :Hello from offline MemoServ")
            line = alice.expect("Memo #")
            assert "sent to Bob" in line, line
            alice.send("PRIVMSG MemoServ :STATUS")
            alice.expect("You have 0 unread memos.")
        finally:
            if bob is not None:
                bob.close()
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

            traveler.send("MEMOSERV STATUS")
            traveler.expect("You must identify to NickServ")

            traveler.send("IDENTIFY Bob bobpass")
            traveler.expect("Password accepted - you are now identified.")
            traveler.send("MEMOSERV STATUS")
            traveler.expect("You have 1 unread memo.")

            traveler.send("MEMOSERV LIST")
            listing = traveler.expect("UNREAD from Alice")
            memo_id = int(listing.split("#", 1)[1].split(" ", 1)[0])

            traveler.send(f"MEMOSERV READ {memo_id}")
            traveler.expect("Hello from offline MemoServ")
            traveler.send("MEMOSERV STATUS")
            traveler.expect("You have 0 unread memos.")

            traveler.send(f"MEMOSERV DEL {memo_id}")
            traveler.expect("Memo deleted.")
            traveler.send("MEMOSERV LIST")
            traveler.expect("You have no memos.")

            # MemoServ remains virtual and must never occupy a normal nick slot.
            traveler.send("ISON MemoServ")
            line = traveler.expect(" 303 Traveler :")
            assert "MemoServ" not in line.split(":", 2)[-1], line
        finally:
            if traveler is not None:
                traveler.close()
            stop(proc)


if __name__ == "__main__":
    main()
