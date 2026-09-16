#!/usr/bin/env python3
"""Reserved nickname policy integration coverage."""

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


def assert_current_nick(client, nick):
    client.send(f"MODE {nick}")
    client.expect(f" 221 {nick} ")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_reserved_nicks.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-reserved-nicks-") as td:
        port = free_port()
        admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write("reserved_nicks = Reserved,Root,OperServ\n")
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

            service = IRCClient(port); clients.append(service)
            service.expect("Looking up your hostname")
            service.send("NICK NickServ")
            service.expect(" 437 * NickServ :is a reserved nick")
            service.send("NICK nIcKsErV")
            service.expect(" 437 * nIcKsErV :is a reserved nick")
            service.send("NICK Normal")
            service.send("USER normal 0 * :Normal User")
            service.expect(" 001 Normal ")
            service.send("NICK mEmOsErV")
            service.expect(" 437 Normal mEmOsErV :is a reserved nick")
            assert_current_nick(service, "Normal")

            ordinary = IRCClient(port); clients.append(ordinary)
            ordinary.expect("Looking up your hostname")
            ordinary.send("NICK Reserved")
            ordinary.expect(" 437 * Reserved :is a reserved nick")
            ordinary.send("NICK reserved")
            ordinary.expect(" 437 * reserved :is a reserved nick")
            ordinary.send("NICK Visitor")
            ordinary.send("USER visitor 0 * :Visitor User")
            ordinary.expect(" 001 Visitor ")
            ordinary.send("NICK ROOT")
            ordinary.expect(" 437 Visitor ROOT :is a reserved nick")
            assert_current_nick(ordinary, "Visitor")

            admin = IRCClient(port); clients.append(admin)
            admin.expect("Looking up your hostname")
            register(admin, "Admin")
            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin ")
            admin.send("NICK reserved")
            assert_current_nick(admin, "reserved")
            admin.send("NICKSERV REGISTER adminpass")
            admin.expect("Nickname registered and identified.")
            admin.send("NICK ChanServ")
            admin.expect(" 437 reserved ChanServ :is a reserved nick")
            assert_current_nick(admin, "reserved")
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
