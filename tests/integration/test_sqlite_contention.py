#!/usr/bin/env python3
"""Verify SQLite writer contention cannot stall the IRC event loop for seconds."""

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
        self.sock.settimeout(0.1)
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
                data = self.sock.recv(8192)
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


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_sqlite_contention.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-sqlite-lock-") as td:
        port = free_port()
        history_db = os.path.join(td, "history.db")
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 16\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\n")
            f.write(f"chanserv_db = {td}/chanserv.db\n")
            f.write(f"memoserv_db = {td}/memoserv.db\n")
            f.write(f"history_db = {history_db}\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        locker = None
        try:
            wait_listen(port, proc)
            writer = IRCClient(port); clients.append(writer)
            probe = IRCClient(port); clients.append(probe)
            register(writer, "Writer")
            register(probe, "Probe")
            writer.send("JOIN #lock")
            writer.expect(" 366 Writer #lock ")
            probe.send("JOIN #lock")
            probe.expect(" 366 Probe #lock ")

            # Force creation/opening of history.db before taking an external
            # writer lock, and verify the common open policy selected WAL.
            writer.send("PRIVMSG #lock :seed")
            probe.expect("PRIVMSG #lock :seed")
            deadline = time.monotonic() + 2.0
            while not os.path.exists(history_db) and time.monotonic() < deadline:
                time.sleep(0.02)
            assert os.path.exists(history_db), "history database was not created"

            locker = sqlite3.connect(history_db, timeout=0.1)
            mode = locker.execute("PRAGMA journal_mode").fetchone()[0].lower()
            assert mode == "wal", f"expected WAL journal mode, got {mode!r}"
            locker.execute("BEGIN IMMEDIATE")

            # history_store runs on ScratchIRCd's event-loop thread. With the
            # old 1-second busy timeout this delayed unrelated PING handling by
            # roughly a second. The common policy caps that wait at 250 ms.
            started = time.monotonic()
            writer.send("PRIVMSG #lock :contended")
            probe.send("PING :contention-probe")
            probe.expect("PONG test.local ::contention-probe", duration=2.0)
            elapsed = time.monotonic() - started
            assert elapsed < 0.85, f"SQLite lock stalled event loop for {elapsed:.3f}s"
        finally:
            if locker is not None:
                try:
                    locker.rollback()
                except sqlite3.Error:
                    pass
                locker.close()
            for client in clients:
                client.close()
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3.0)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=3.0)


if __name__ == "__main__":
    main()
