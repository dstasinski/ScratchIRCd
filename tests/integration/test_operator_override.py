#!/usr/bin/env python3
"""End-to-end tests for SA*/SET* and RESTART operator commands."""

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
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def wait_listen(port, proc, duration=5.0):
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            stderr = proc.stderr.read() if proc.stderr else ""
            raise RuntimeError(f"server exited: {stderr!r}")
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            s.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not listen")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


def stop_server(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_operator_override.py scratchircd scratchircd-mkpasswd")

    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-override-") as tmp:
        port = free_port()
        data = os.path.join(tmp, "data")
        os.makedirs(data)
        config = os.path.join(tmp, "ircd.conf")
        motd = os.path.join(tmp, "motd.txt")
        rules = os.path.join(tmp, "rules.txt")
        password_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        open(motd, "w", encoding="utf-8").write("override test\n")
        open(rules, "w", encoding="utf-8").write("rule\n")
        with open(config, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"motd_file = {motd}\nrules_file = {rules}\n")
            f.write(f"operators_db = {data}/operators.db\n")
            f.write(f"bans_db = {data}/bans.db\n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {password_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, config], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)
            admin = IRCClient(port); clients.append(admin)
            bob = IRCClient(port); clients.append(bob)
            register(admin, "alice")
            register(bob, "bob")
            admin.send("OPER root adminpass")
            admin.expect(" 381 alice :You are now a Network Administrator")

            # Forced join/part bypasses ordinary channel policy.
            admin.send("JOIN #forced")
            admin.expect(" JOIN #forced")
            admin.send("MODE #forced +ik secret")
            admin.expect(" MODE #forced +ik secret")
            admin.send("SAJOIN bob #forced")
            bob.expect(" JOIN #forced")

            admin.send("SAMODE #forced +v bob")
            bob.expect(" MODE #forced +v bob")
            admin.send("SAMODE bob +i")
            admin.expect("NOTICE alice :SAMODE completed for bob")
            bob.send("MODE bob")
            bob.expect(" 221 bob +i")

            # SET* mutations become the public identity but leave real address intact.
            admin.send("SETHOST bob staff.example.test")
            admin.expect("NOTICE alice :SETHOST bob -> staff.example.test")
            admin.send("SETIDENT bob helper")
            admin.expect("NOTICE alice :SETIDENT bob -> helper")
            admin.send("SETNAME bob :Helpful Bob")
            admin.expect("NOTICE alice :SETNAME completed for bob")

            admin.send("WHOIS bob")
            whois = admin.expect(" 318 alice bob ")
            assert any(" 311 alice bob helper staff.example.test * :Helpful Bob" in line
                       for line in whois), whois
            assert any(" 378 alice bob :is connecting from *@" in line and "127.0.0.1" in line
                       for line in whois), whois

            admin.send("SAPART bob #forced")
            bob.expect(" PART #forced :Forced part by alice")

            # Restart disconnects current users, reloads config, and relistens.
            admin.send("RESTART")
            admin.expect("NOTICE alice :Restarting ScratchIRCd")
            time.sleep(0.4)
            wait_listen(port, proc, 5.0)
            after = IRCClient(port); clients.append(after)
            register(after, "after")
        finally:
            for client in clients:
                client.close()
            stop_server(proc)

    print("operator override integration tests passed")


if __name__ == "__main__":
    main()
