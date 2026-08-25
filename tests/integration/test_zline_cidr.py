#!/usr/bin/env python3
"""End-to-end IPv4 CIDR ZLINE coverage, including WebIRC end-user identity."""

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
            time.sleep(0.1)
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not listen")


def register_direct(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


def register_webirc(client, nick, real_ip):
    client.send(f"WEBIRC gateway-secret web.example user.example {real_ip}")
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3.0)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_zline_cidr.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-zline-cidr-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
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
            f.write("webirc_gateway = 127.0.0.1 gateway-secret\n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)
            admin = IRCClient(port); clients.append(admin)
            register_direct(admin, "Admin")
            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin :You are now a Network Administrator")

            inside = IRCClient(port); clients.append(inside)
            register_webirc(inside, "Inside", "203.0.113.25")
            inside.expect(" 001 Inside ")

            outside = IRCClient(port); clients.append(outside)
            register_webirc(outside, "Outside", "203.0.114.25")
            outside.expect(" 001 Outside ")

            # Malformed CIDR masks must be rejected and never persisted.
            admin.send("ZLINE 203.0.113.0/33 :invalid prefix")
            admin.expect("Invalid ZLINE mask: 203.0.113.0/33")

            # A CIDR ZLINE immediately disconnects matching current clients.
            admin.send("ZLINE 203.0.113.0/24 :CIDR test")
            admin.expect("ZLINE added: 203.0.113.0/24")
            inside.expect(" 465 Inside ")

            # Adjacent subnet remains connected.
            outside.send("PING :outside-still-here")
            outside.expect("PONG")

            # The persisted CIDR is applied to later WebIRC end-user real_ip.
            later = IRCClient(port); clients.append(later)
            register_webirc(later, "Later", "203.0.113.99")
            later.expect(" 465 Later ")

            allowed = IRCClient(port); clients.append(allowed)
            register_webirc(allowed, "Allowed", "203.0.114.99")
            allowed.expect(" 001 Allowed ")

            admin.send("ZLINE -203.0.113.0/24")
            admin.expect("ZLINE removed: 203.0.113.0/24")

            after_remove = IRCClient(port); clients.append(after_remove)
            register_webirc(after_remove, "AfterRemove", "203.0.113.88")
            after_remove.expect(" 001 AfterRemove ")
        finally:
            for client in clients:
                client.close()
            stop(proc)

    print("CIDR ZLINE integration tests passed")


if __name__ == "__main__":
    main()
