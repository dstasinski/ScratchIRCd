#!/usr/bin/env python3
"""E-LINE exact user@IP matching integration coverage."""

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
                    return got
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {got!r}")

    def read_for(self, duration=1.0):
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
            except ConnectionResetError:
                break
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
    raise RuntimeError("listener did not start")


def register(client, nick, user=None):
    user = user or nick
    client.send(f"NICK {nick}")
    client.send(f"USER {user} 0 * :{nick}")
    client.expect(f" 001 {nick} ")


def assert_still_connected(client):
    client.send("PING :eline-exact-probe")
    lines = client.expect("eline-exact-probe")
    assert any(" PONG " in line and "eline-exact-probe" in line for line in lines), lines


def assert_disconnected(client):
    lines = client.read_for(2.0)
    assert any(" 465 " in line or "ERROR" in line for line in lines), lines


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_eline_exact_identity.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-eline-exact-") as td:
        port = free_port()
        admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
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
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@*\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)

            admin = IRCClient(port); clients.append(admin)
            register(admin, "Admin")
            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin ")
            admin.send("ELINE friend@127.0.0.1 k 0 :single identity exception")
            admin.expect("NOTICE Admin :ELINE added: friend@127.0.0.1 k")

            friend = IRCClient(port); clients.append(friend)
            register(friend, "Friend", "friend")
            enemy = IRCClient(port); clients.append(enemy)
            register(enemy, "Enemy", "enemy")

            admin.send("KLINE *@127.0.0.1 :local test ban")
            admin.expect("NOTICE Admin :KLINE added: *@127.0.0.1")

            assert_still_connected(friend)
            assert_disconnected(enemy)
        finally:
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
