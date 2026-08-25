#!/usr/bin/env python3
"""REHASH consistency coverage for live-safe and restart-required settings."""

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
            time.sleep(0.1)
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not listen")


def write_conf(path, port, admin_hash, td, *, admin1="Before Rehash",
               bans_db=None, nospoof=False, server_password=""):
    if bans_db is None:
        bans_db = os.path.join(td, "bans.db")
    with open(path, "w", encoding="utf-8") as f:
        f.write("server_name = test.local\nnetwork_name = TestNet\n")
        f.write("bind_address = 127.0.0.1\n")
        f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
        f.write(f"operators_db = {td}/operators.db\n")
        f.write(f"bans_db = {bans_db}\n")
        f.write(f"nickserv_db = {td}/nickserv.db\nchanserv_db = {td}/chanserv.db\n")
        f.write(f"memoserv_db = {td}/memoserv.db\nhistory_db = {td}/history.db\n")
        f.write("geoip_city_db = \ngeoip_asn_db = \n")
        f.write(f"admin_location1 = {admin1}\nadmin_location2 = Test Location\n")
        f.write("admin_email = admin@example.test\n")
        f.write(f"netadmin_name = root\nnetadmin_password_hash = {admin_hash}\n")
        f.write("netadmin_hostmask = *!*@127.0.0.1\n")
        if nospoof:
            f.write("nospoof = 1\nnospoof_timeout_seconds = 10\n")
        if server_password:
            f.write(f"server_password = {server_password}\n")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait(timeout=3.0)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_rehash_consistency.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-rehash-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        write_conf(conf, port, admin_hash, td)

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        client = None
        try:
            wait_listen(port, proc)
            client = IRCClient(port)
            client.send("NICK Admin")
            client.send("USER admin 0 * :Admin User")
            client.expect(" 001 Admin ")
            client.send("OPER root adminpass")
            client.expect(" 381 Admin :You are now a Network Administrator")

            # A live-safe text change applies immediately.
            write_conf(conf, port, admin_hash, td, admin1="After Rehash")
            client.send("REHASH")
            client.expect(" 382 Admin ")
            client.send("ADMIN")
            client.expect(" 257 Admin :After Rehash")

            # Switching persistent databases under live state is rejected.
            write_conf(conf, port, admin_hash, td, admin1="Should Not Apply",
                       bans_db=os.path.join(td, "other-bans.db"))
            client.send("REHASH")
            client.expect("REHASH rejected: startup-bound, persistent-store, or registration-gate change requires RESTART")
            client.send("ADMIN")
            client.expect(" 257 Admin :After Rehash")

            # Registration gates likewise require a restart.
            write_conf(conf, port, admin_hash, td, admin1="NoSpoof Should Not Apply",
                       nospoof=True)
            client.send("REHASH")
            client.expect("REHASH rejected: startup-bound, persistent-store, or registration-gate change requires RESTART")
            client.send("ADMIN")
            client.expect(" 257 Admin :After Rehash")

            write_conf(conf, port, admin_hash, td, admin1="Password Should Not Apply",
                       server_password="newpass")
            client.send("REHASH")
            client.expect("REHASH rejected: startup-bound, persistent-store, or registration-gate change requires RESTART")
            client.send("ADMIN")
            client.expect(" 257 Admin :After Rehash")
        finally:
            if client is not None:
                client.close()
            stop(proc)


if __name__ == "__main__":
    main()
