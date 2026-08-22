#!/usr/bin/env python3
"""End-to-end KNOCK and +K policy tests."""

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
        self.pending = []

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def _next_line(self, deadline):
        if self.pending:
            return self.pending.pop(0)
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                return raw.rstrip(b"\r").decode(errors="replace")
            try:
                data = self.sock.recv(4096)
                if not data:
                    return None
                self.buffer += data
            except socket.timeout:
                pass
        return None

    def expect(self, needle, duration=3.0):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            line = self._next_line(deadline)
            if line is None:
                continue
            got.append(line)
            if needle in line:
                return line
        raise AssertionError(f"expected {needle!r}; got {got!r}")

    def expect_not(self, needle, duration=0.5):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            line = self._next_line(deadline)
            if line is None:
                continue
            got.append(line)
            if needle in line:
                raise AssertionError(f"unexpected {needle!r}; got {got!r}")
        return got

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
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_knock.py scratchircd")

    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-knock-") as tmp:
        port = free_port()
        conf = os.path.join(tmp, "ircd.conf")
        motd = os.path.join(tmp, "motd.txt")
        rules = os.path.join(tmp, "rules.txt")
        data = os.path.join(tmp, "data")
        os.mkdir(data)
        open(motd, "w", encoding="utf-8").write("test\n")
        open(rules, "w", encoding="utf-8").write("test\n")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 16\ndns_timeout_seconds = 1\n")
            f.write(f"motd_file = {motd}\nrules_file = {rules}\n")
            f.write(f"operators_db = {data}/operators.db\n")
            f.write(f"bans_db = {data}/bans.db\n")
            f.write(f"nickserv_db = {data}/nickserv.db\n")
            f.write(f"chanserv_db = {data}/chanserv.db\n")
            f.write(f"memoserv_db = {data}/memoserv.db\n")
            f.write(f"history_db = {data}/history.db\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)
            owner = IRCClient(port); clients.append(owner)
            guest = IRCClient(port); clients.append(guest)
            register(owner, "Owner")
            register(guest, "Guest")

            owner.send("JOIN #knock")
            owner.expect(" JOIN #knock")
            owner.send("MODE #knock +i")
            owner.expect(" MODE #knock +i")

            guest.send("KNOCK #knock :please invite me")
            owner.expect(" KNOCK #knock :please invite me")
            guest.expect("NOTICE Guest :KNOCK delivered to #knock channel staff")

            # KNOCK itself must not create an invitation.
            guest.send("JOIN #knock")
            guest.expect(" 473 Guest #knock ")

            owner.send("INVITE Guest #knock")
            owner.expect(" 341 Owner Guest #knock")
            guest.expect(" INVITE Guest :#knock")
            guest.send("JOIN #knock")
            guest.expect(" JOIN #knock")

            guest.send("KNOCK #knock :already here")
            guest.expect(" 480 Guest :Cannot knock on #knock (you are already on the channel)")

            guest.send("PART #knock")
            guest.expect(" PART #knock ")
            owner.send("MODE #knock +K")
            owner.expect(" MODE #knock +K")
            guest.send("KNOCK #knock :blocked")
            guest.expect(" 480 Guest :Cannot knock on #knock (channel mode +K)")
            owner.expect_not(" KNOCK #knock :blocked")

            owner.send("MODE #knock -Ki")
            owner.expect(" MODE #knock -Ki")
            guest.send("KNOCK #knock :open")
            guest.expect(" 480 Guest :Cannot knock on #knock (channel is open)")
        finally:
            for client in clients:
                client.close()
            stop_server(proc)


if __name__ == "__main__":
    main()
