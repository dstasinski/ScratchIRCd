#!/usr/bin/env python3
"""End-to-end tests for permission-controlled operator actions."""

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
            raise RuntimeError(f"server exited early: {stderr!r}")
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
        raise SystemExit("usage: test_operator_actions.py scratchircd scratchircd-mkpasswd")

    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-oper-actions-") as tmp:
        port = free_port()
        data_dir = os.path.join(tmp, "data")
        config = os.path.join(tmp, "ircd.conf")
        motd = os.path.join(tmp, "motd.txt")
        rules = os.path.join(tmp, "rules.txt")
        admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()

        open(motd, "w", encoding="utf-8").write("test\n")
        open(rules, "w", encoding="utf-8").write("test\n")
        with open(config, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"motd_file = {motd}\nrules_file = {rules}\n")
            f.write("admin_email = admin@example.test\n")
            f.write(f"operators_db = {data_dir}/operators.db\n")
            f.write(f"bans_db = {data_dir}/bans.db\n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, config], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)
            admin = IRCClient(port); clients.append(admin)
            receiver = IRCClient(port); clients.append(receiver)
            register(admin, "alice")
            register(receiver, "bob")
            admin.send("OPER root adminpass")
            admin.expect(" 381 alice :You are now a Network Administrator")

            receiver.send("MODE bob +w")
            receiver.expect(" MODE bob +w")
            admin.send("WALLOPS :maintenance test")
            receiver.expect(" WALLOPS :maintenance test")

            victim = IRCClient(port); clients.append(victim)
            register(victim, "killme")
            admin.send("KILL killme :operator test")
            victim.expect(" KILL killme :operator test")

            admin.send("KLINE blocked@127.0.0.1 :kline test")
            admin.expect("NOTICE alice :KLINE added: blocked@127.0.0.1")
            blocked = IRCClient(port); clients.append(blocked)
            blocked.send("NICK blocked")
            blocked.send("USER blocked 0 * :Blocked User")
            blocked.expect(" 465 blocked ")
            admin.send("KLINE -blocked@127.0.0.1")
            admin.expect("NOTICE alice :KLINE removed: blocked@127.0.0.1")

            admin.send("ZLINE 127.0.0.1 :zline test")
            admin.expect("NOTICE alice :ZLINE added: 127.0.0.1")
            zed = IRCClient(port); clients.append(zed)
            zed.send("NICK zed")
            zed.send("USER zed 0 * :Zed User")
            zed.expect(" 465 zed ")
            admin.send("ZLINE -127.0.0.1")
            admin.expect("NOTICE alice :ZLINE removed: 127.0.0.1")

            admin.send("REHASH")
            admin.expect(" 382 alice ")
        finally:
            for client in clients:
                client.close()
            stop_server(proc)

    print("operator action integration tests passed")


if __name__ == "__main__":
    main()
