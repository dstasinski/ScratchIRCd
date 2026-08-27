#!/usr/bin/env python3
"""End-to-end validation of in-process RESTART lifecycle state reset."""

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
        self.sock.settimeout(0.2)
        self.buffer = b""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def read_until(self, needle, duration=4.0):
        deadline = time.monotonic() + duration
        lines = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode(errors="replace")
                lines.append(line)
                if needle in line:
                    return lines
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {lines!r}")

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
            stderr = proc.stderr.read() if proc.stderr else ""
            raise RuntimeError(f"server exited during restart: {stderr!r}")
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            sock.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not resume listening")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick}")
    lines = client.read_until(f" 001 {nick} ")
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        try:
            data = client.sock.recv(4096)
            if not data:
                break
            client.buffer += data
        except socket.timeout:
            break
    while b"\n" in client.buffer:
        raw, client.buffer = client.buffer.split(b"\n", 1)
        lines.append(raw.rstrip(b"\r").decode(errors="replace"))
    return lines


def pchannels(lines, nick):
    names = []
    for line in lines:
        if f" 005 {nick} " not in line:
            continue
        payload = line.split(f" 005 {nick} ", 1)[1]
        payload = payload.rsplit(" :are supported by this server", 1)[0]
        for token in payload.split():
            if token.startswith("PCHANNELS="):
                names.extend(name for name in token.split("=", 1)[1].split(",") if name)
    return names


def irc_fold(value):
    return value.lower().translate(str.maketrans("{}|~", "[]\\^"))


def irc_nocase(left, right):
    a, b = irc_fold(left), irc_fold(right)
    return (a > b) - (a < b)


def open_chanserv_db(path):
    db = sqlite3.connect(path)
    db.create_collation("IRCNOCASE", irc_nocase)
    return db


def stop_server(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_restart.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-restart-") as td:
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
        clients = []
        original_pid = proc.pid
        try:
            wait_listen(port, proc)

            warm = IRCClient(port); clients.append(warm)
            warm_lines = register(warm, "Warm")
            assert pchannels(warm_lines, "Warm") == [], warm_lines

            # Change persistent state behind the process-local cache without
            # calling ChanServ, so its generation intentionally does not move.
            db = open_chanserv_db(chanserv_db)
            try:
                db.execute(
                    "INSERT INTO channels(name,founder,description,enabled) VALUES(?,?,?,1)",
                    ("#AfterRestart", "founder", "restart cache test"))
                db.commit()
            finally:
                db.close()

            cached = IRCClient(port); clients.append(cached)
            cached_lines = register(cached, "Cached")
            assert pchannels(cached_lines, "Cached") == [], cached_lines

            admin = IRCClient(port); clients.append(admin)
            register(admin, "Admin")
            admin.send("OPER root adminpass")
            admin.read_until(" 381 Admin :You are now a Network Administrator")
            admin.send("RESTART")
            admin.read_until("NOTICE Admin :Restarting ScratchIRCd")

            # Existing clients are disconnected by teardown. Retry a fresh
            # registration until the same process has recreated its listener.
            deadline = time.monotonic() + 5.0
            post_lines = None
            while time.monotonic() < deadline and proc.poll() is None:
                try:
                    post = IRCClient(port)
                    clients.append(post)
                    post_lines = register(post, "After")
                    break
                except (OSError, AssertionError):
                    time.sleep(0.05)
            assert proc.poll() is None, "RESTART exited the daemon"
            assert proc.pid == original_pid, "RESTART replaced the process"
            assert post_lines is not None, "server did not accept registration after RESTART"
            assert pchannels(post_lines, "After") == ["#AfterRestart"], post_lines
        finally:
            for client in clients:
                client.close()
            stop_server(proc)


if __name__ == "__main__":
    main()
