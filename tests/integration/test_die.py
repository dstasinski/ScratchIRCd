#!/usr/bin/env python3
"""End-to-end test for permission-gated graceful DIE shutdown."""

import os
import signal
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

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def expect(self, needle, duration=3.0):
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


def ircnocase(left, right):
    left = left.lower().translate(str.maketrans({"{": "[", "}": "]", "|": "\\", "~": "^"}))
    right = right.lower().translate(str.maketrans({"{": "[", "}": "]", "|": "\\", "~": "^"}))
    return (left > right) - (left < right)


def seed_log_backlog(path, rows):
    db = sqlite3.connect(path)
    try:
        db.create_collation("IRCNOCASE", ircnocase)
        old = int(time.time()) - 600
        db.executemany(
            "INSERT INTO channel_log_queue(channel,event_time,body) VALUES(?,?,?)",
            (("#backlog", old, f"backlog row {index}") for index in range(rows)),
        )
        db.commit()
    finally:
        db.close()


def queued_rows(path):
    db = sqlite3.connect(path)
    try:
        return db.execute("SELECT COUNT(*) FROM channel_log_queue").fetchone()[0]
    finally:
        db.close()


def clear_queued_rows(path):
    db = sqlite3.connect(path)
    try:
        db.execute("DELETE FROM channel_log_queue")
        db.commit()
    finally:
        db.close()


def assert_signal_shutdown(binary, conf, port, sig, nick):
    proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    client = None
    try:
        wait_listen(port, proc)
        client = IRCClient(port)
        register(client, nick)
        proc.send_signal(sig)
        try:
            rc = proc.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            raise AssertionError(f"signal {sig} did not terminate the daemon")
        assert rc == 0, f"signal {sig} exited with status {rc}: {proc.stderr.read()}"
    finally:
        if client is not None:
            client.close()
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=3)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_die.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-die-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        chanserv_db = os.path.join(td, "chanserv.db")
        admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 16\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\nbans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\nchanserv_db = {chanserv_db}\n")
            f.write(f"memoserv_db = {td}/memoserv.db\nhistory_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        ordinary = admin = None
        try:
            wait_listen(port, proc)
            ordinary = IRCClient(port); register(ordinary, "Ordinary")
            admin = IRCClient(port); register(admin, "Admin")

            ordinary.send("DIE")
            ordinary.expect(" 481 Ordinary ")
            assert proc.poll() is None, "unauthorized DIE stopped the daemon"

            # Seed a substantial due backlog, then require DIE to complete
            # promptly and cleanly. Automatic bounded flush passes may run while
            # the client is waiting for the shutdown reply, so final queue size
            # cannot be used to infer the per-pass bound. That bound is covered
            # deterministically by test_channel_log without a concurrent loop.
            seed_log_backlog(chanserv_db, 2048)
            assert queued_rows(chanserv_db) == 2048

            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin :You are now a Network Administrator")
            admin.send("DIE")
            admin.expect("NOTICE Admin :Shutting down ScratchIRCd")

            try:
                rc = proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                raise AssertionError("DIE did not terminate the daemon")
            assert rc == 0, f"daemon exited with status {rc}: {proc.stderr.read()}"

            remaining = queued_rows(chanserv_db)
            assert remaining < 2048, (
                f"durable backlog was not processed during graceful shutdown: remaining={remaining}"
            )
        finally:
            if ordinary is not None: ordinary.close()
            if admin is not None: admin.close()
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill(); proc.wait(timeout=3)

        # Exercise the asynchronous signal path while the event loop is known
        # to be active. The handler must request the same graceful teardown as
        # DIE without touching non-signal-safe ordinary process state.
        clear_queued_rows(chanserv_db)
        assert_signal_shutdown(binary, conf, port, signal.SIGTERM, "TermClient")
        assert_signal_shutdown(binary, conf, port, signal.SIGINT, "IntClient")


if __name__ == "__main__":
    main()
