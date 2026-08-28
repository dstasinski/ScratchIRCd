#!/usr/bin/env python3
"""Focused security coverage for privileged public-identity mutations."""

import os
import socket
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
    raise RuntimeError("server did not listen")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait(timeout=3)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_operator_identity_security.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-identsec-") as td:
        port = free_port()
        passwd = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 16\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\nbans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\nchanserv_db = {td}/chanserv.db\n")
            f.write(f"memoserv_db = {td}/memoserv.db\nhistory_db = {td}/history.db\n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {passwd}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        admin = target = None
        try:
            wait_listen(port, proc)
            admin = IRCClient(port); register(admin, "Admin")
            target = IRCClient(port); register(target, "Target")
            admin.send("OPER root adminpass"); admin.expect(" 381 Admin ")
            target.send("NICKSERV REGISTER targetpass")
            target.expect("Nickname registered and identified.")

            # Prefix delimiters and other unsafe syntax must be rejected.
            admin.send("SETHOST Target bad@host")
            admin.expect(" 461 Admin SETHOST ")
            admin.send("SETHOST Target bad!host")
            admin.expect(" 461 Admin SETHOST ")
            admin.send("SETIDENT Target bad@ident")
            admin.expect(" 461 Admin SETIDENT ")
            admin.send("SETIDENT Target bad!ident")
            admin.expect(" 461 Admin SETIDENT ")

            # Valid public mutations work without altering the authenticated account
            # or the operator-visible real address.
            admin.send("SETHOST Target staff.example.test")
            admin.expect("NOTICE Admin :SETHOST Target -> staff.example.test")
            admin.send("SETIDENT Target helper")
            admin.expect("NOTICE Admin :SETIDENT Target -> helper")
            admin.send("SETNAME Target :Helpful Target")
            admin.expect("NOTICE Admin :SETNAME completed for Target")
            admin.send("WHOIS Target")
            whois = admin.expect(" 318 Admin Target ")
            assert any(" 311 Admin Target helper staff.example.test * :Helpful Target" in line
                       for line in whois), whois
            assert any(" 307 Admin Target " in line or "is logged in as Target" in line
                       for line in whois), whois
            assert any(" 378 Admin Target :is connecting from *@" in line and "127.0.0.1" in line
                       for line in whois), whois

            # User SAMODE must not manufacture security/provenance modes.
            admin.send("SAMODE Target +orzxtV")
            admin.expect("NOTICE Admin :SAMODE completed for Target")
            target.send("MODE Target")
            modes = target.expect(" 221 Target ")
            token = next(line.split(" 221 Target ", 1)[1].split()[0]
                         for line in modes if " 221 Target " in line)
            # +r is legitimate from NickServ and +t is legitimate from the
            # earlier SETHOST. SAMODE must not manufacture oper/TLS/cloak/
            # WebIRC provenance modes or remove the existing vhost mode.
            assert "r" in token and "t" in token, token
            assert "o" not in token and "z" not in token and "x" not in token and "V" not in token, token
        finally:
            if admin is not None: admin.close()
            if target is not None: target.close()
            stop(proc)


if __name__ == "__main__":
    main()
