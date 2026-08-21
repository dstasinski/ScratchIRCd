#!/usr/bin/env python3
"""End-to-end NickServ account, IDENTIFY, vhost and service-visibility tests."""

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


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_nickserv.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-nickserv-") as td:
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
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@*\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)

            alice = IRCClient(port); clients.append(alice)
            register(alice, "Alice")
            alice.send("PRIVMSG NickServ :REGISTER firstpass")
            alice.expect("Nickname registered and identified.")
            alice.send("MODE Alice")
            modes = alice.expect(" 221 Alice ")
            assert any("r" in line.rsplit(" ", 1)[-1]
                       for line in modes if " 221 Alice " in line), modes

            # NickServ is addressable but not represented as an online Client.
            alice.send("ISON NickServ Alice")
            ison = alice.expect(" 303 Alice ")
            ison_line = next(line for line in ison if " 303 Alice " in line)
            assert "Alice" in ison_line and "NickServ" not in ison_line, ison

            # Virtual service names are reserved with the supplied reserved-nick numeric.
            impostor = IRCClient(port); clients.append(impostor)
            impostor.send("NICK NickServ")
            impostor.expect(" 437 ")
            impostor.close(); clients.remove(impostor)

            # Self-service password change uses only the stored account identity.
            alice.send("PRIVMSG NickServ :SET PASSWORD secondpass")
            alice.expect("Password changed.")
            alice.send("QUIT :reconnect")
            alice.close(); clients.remove(alice)

            user = IRCClient(port); clients.append(user)
            register(user, "Traveler")
            user.send("IDENTIFY Alice firstpass")
            user.expect("Password incorrect or account unavailable.")
            user.send("IDENTIFY Alice secondpass")
            user.expect("Password accepted - you are now identified.")
            user.send("MODE Traveler")
            modes = user.expect(" 221 Traveler ")
            assert any("r" in line.rsplit(" ", 1)[-1]
                       for line in modes if " 221 Traveler " in line), modes
            user.send("WHOIS Traveler")
            whois = user.expect(" 318 Traveler Traveler ")
            assert any("is logged in as Alice" in line for line in whois), whois

            admin = IRCClient(port); clients.append(admin)
            register(admin, "Admin")
            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin ")
            admin.send("NSSET Alice VHOST registered.example.test")
            admin.expect("NickServ account updated.")
            admin.send("NSINFO Alice")
            admin.expect("vhost=registered.example.test")

            user.send("QUIT :reconnect for vhost")
            user.close(); clients.remove(user)
            user = IRCClient(port); clients.append(user)
            register(user, "Traveler2")
            user.send("IDENTIFY Alice secondpass")
            user.expect("Password accepted - you are now identified.")
            user.send("WHOIS Traveler2")
            whois = user.expect(" 318 Traveler2 Traveler2 ")
            assert any(" 311 Traveler2 Traveler2 " in line and
                       " registered.example.test " in line for line in whois), whois
            assert any("is logged in as Alice" in line for line in whois), whois

            admin.send("NSSET Alice ENABLED 0")
            admin.expect("NickServ account updated.")
            fresh = IRCClient(port); clients.append(fresh)
            register(fresh, "Fresh")
            fresh.send("IDENTIFY Alice secondpass")
            fresh.expect("Password incorrect or account unavailable.")
            admin.send("NSSET Alice ENABLED 1")
            admin.expect("NickServ account updated.")
            admin.send("NSDROP Alice")
            admin.expect("NickServ account deleted.")
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
