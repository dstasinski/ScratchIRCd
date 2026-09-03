#!/usr/bin/env python3
"""End-to-end coverage for IRCop/admin FLASH numeric delivery."""

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

    def collect(self, duration=0.3):
        deadline = time.monotonic() + duration
        lines = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                lines.append(raw.rstrip(b"\r").decode(errors="replace"))
            try:
                data = self.sock.recv(8192)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            lines.append(raw.rstrip(b"\r").decode(errors="replace"))
        return lines

    def expect(self, needle, duration=5.0):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
            seen.extend(self.collect(0.1))
            if any(needle in line for line in seen):
                return seen
        raise AssertionError(f"expected {needle!r}; got {seen!r}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def wait_listen(port, process):
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(process.stderr.read())
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not listen")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3.0)


def assert_no_flash(client, text):
    lines = client.collect(0.5)
    assert not any(" 343 " in line and text in line for line in lines), lines


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_flash.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-flash-") as td:
        port = free_port()
        config = os.path.join(td, "ircd.conf")
        admin_hash = subprocess.check_output(
            [mkpasswd, "adminpass"], text=True
        ).strip()
        with open(config, "w", encoding="utf-8") as handle:
            handle.write("server_name = test.local\nnetwork_name = TestNet\n")
            handle.write("bind_address = 127.0.0.1\n")
            handle.write(f"port = {port}\nmax_clients = 16\ndns_timeout_seconds = 1\n")
            handle.write("nospoof = no\ngeoip_city_db = \ngeoip_asn_db = \n")
            for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
                handle.write(f"{name}_db = {td}/{name}.db\n")
            handle.write("netadmin_name = root\n")
            handle.write(f"netadmin_password_hash = {admin_hash}\n")
            handle.write("netadmin_hostmask = *!*@127.0.0.1\n")

        process = subprocess.Popen(
            [binary, config], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        clients = []
        try:
            wait_listen(port, process)
            admin = IRCClient(port); clients.append(admin); register(admin, "Admin")
            ircop = IRCClient(port); clients.append(ircop); register(ircop, "Oper")
            alice = IRCClient(port); clients.append(alice); register(alice, "Alice")
            bob = IRCClient(port); clients.append(bob); register(bob, "Bob")
            outsider = IRCClient(port); clients.append(outsider); register(outsider, "Outside")

            outsider.send("FLASH * :not authorized")
            outsider.expect(" 481 Outside ")

            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin :You are now a Network Administrator")
            admin.send("OPERADD announcer operpass - :-")
            admin.expect("NOTICE Admin :Operator added")
            ircop.send("OPER announcer operpass")
            ircop.expect(" 381 Oper :You are now an IRC operator")

            for member in (admin, alice, bob):
                member.send("JOIN #flash")
                member.expect(" 366 ")
            for connected in clients:
                connected.collect()

            admin.send("FLASH #flash :channel announcement")
            admin.expect(":test.local 343 Admin :channel announcement")
            alice.expect(":test.local 343 Alice :channel announcement")
            bob.expect(":test.local 343 Bob :channel announcement")
            assert_no_flash(ircop, "channel announcement")
            assert_no_flash(outsider, "channel announcement")

            ircop.send("FLASH Alice,Bob :direct announcement")
            alice.expect(":test.local 343 Alice :direct announcement")
            bob.expect(":test.local 343 Bob :direct announcement")
            assert_no_flash(admin, "direct announcement")
            assert_no_flash(outsider, "direct announcement")

            admin.send("FLASH * :network announcement")
            for nick, connected in (("Admin", admin), ("Oper", ircop),
                                    ("Alice", alice), ("Bob", bob),
                                    ("Outside", outsider)):
                connected.expect(f":test.local 343 {nick} :network announcement")

            ircop.send("FLASH Missing :unknown target")
            ircop.expect(" 401 Oper Missing ")
            ircop.send("FLASH #missing :unknown channel")
            ircop.expect(" 403 Oper #missing ")
            ircop.send("FLASH Alice :" + ("x" * 490))
            ircop.expect(" 417 Oper FLASH :Message would exceed the IRC line limit")
            assert_no_flash(alice, "x" * 490)

            assert process.poll() is None
        finally:
            for connected in clients:
                connected.close()
            stop(process)

    print("FLASH integration tests passed")


if __name__ == "__main__":
    main()
