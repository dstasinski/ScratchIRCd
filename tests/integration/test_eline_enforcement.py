#!/usr/bin/env python3
"""E-LINE exemption enforcement for KLINE and ZLINE."""

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

    def read_for(self, duration=0.4):
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


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick}")
    client.expect(f" 001 {nick} ")


def assert_alive(client, nick):
    client.send(f"PING :{nick}")
    client.expect(f"PONG test.local ::{nick}")


def expect_registration_rejected(port, nick):
    client = IRCClient(port)
    try:
        client.expect("Looking up your hostname")
        client.send(f"NICK {nick}")
        client.send(f"USER {nick} 0 * :{nick}")
        client.expect(" 465 ")
    finally:
        client.close()


def run_case(binary, mkpasswd, ban_command, eline_mask, eline_type, reject_nick):
    with tempfile.TemporaryDirectory(prefix="scratchircd-eline-enforce-") as td:
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
            admin.expect("Looking up your hostname")
            register(admin, "Admin")
            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin ")

            victim = IRCClient(port); clients.append(victim)
            victim.expect("Looking up your hostname")
            register(victim, "Victim")

            admin.send(f"ELINE {eline_mask} {eline_type} 0 :allow local test")
            admin.expect(f"ELINE added: {eline_mask} {eline_type}")
            admin.send(ban_command)
            admin.expect(" added: ")
            assert_alive(victim, "Victim")
            leaked = victim.read_for()
            assert not any("465 Victim" in line or "ERROR" in line for line in leaked), leaked

            admin.send(f"ELINE -{eline_mask}")
            admin.expect(f"ELINE removed: {eline_mask}")
            expect_registration_rejected(port, reject_nick)
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


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_eline_enforcement.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    run_case(binary, mkpasswd,
             "KLINE *@127.0.0.1 :blocked by kline",
             "*@127.0.0.1", "k", "KBlocked")
    run_case(binary, mkpasswd,
             "ZLINE 127.0.0.1 :blocked by zline",
             "127.0.0.1", "z", "ZBlocked")


if __name__ == "__main__":
    main()
