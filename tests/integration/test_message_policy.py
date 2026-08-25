#!/usr/bin/env python3
"""End-to-end coverage for +g/+s and channel +c/+S message policy."""

import os
import socket
import subprocess
import sys
import tempfile
import time


class IRCClient:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
        self.sock.settimeout(0.20)
        self.buffer = b""
        self.pending = []

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def _fill_lines(self):
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            self.pending.append(raw.rstrip(b"\r").decode(errors="replace"))

    def expect(self, needle, duration=5.0):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
            self._fill_lines()
            for i, line in enumerate(self.pending):
                if needle in line:
                    seen.extend(self.pending[:i + 1])
                    del self.pending[:i + 1]
                    return line
            seen.extend(self.pending)
            self.pending.clear()
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {seen!r}")

    def expect_not(self, needle, duration=0.75):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
            self._fill_lines()
            for line in self.pending:
                if needle in line:
                    raise AssertionError(f"unexpected {needle!r}; got {self.pending!r}")
            seen.extend(self.pending)
            self.pending.clear()
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        return seen

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
    raise RuntimeError("listener did not start")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick.lower()} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


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
        raise SystemExit("usage: test_message_policy.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-message-policy-") as td:
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
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)
            admin = IRCClient(port); clients.append(admin)
            user = IRCClient(port); clients.append(user)
            register(admin, "Admin")
            register(user, "User")

            # TARGMAX advertises one PRIVMSG/NOTICE target. Reject a comma list
            # rather than treating it as an implementation-dependent literal.
            user.send("PRIVMSG Admin,User :must not deliver")
            user.expect(" 407 User Admin,User ")
            admin.expect_not("must not deliver")
            user.send("NOTICE Admin,User :must not notice")
            admin.expect_not("must not notice")
            user.send("NOTICE Admin :")
            admin.expect_not("NOTICE Admin :")

            # Channel creation uses one shared syntax validator. Comma/colon
            # names must not create partial or malformed channels.
            user.send("JOIN #bad,name")
            user.expect(" 403 User #bad,name ")
            user.send("JOIN #bad:name")
            user.expect(" 403 User #bad:name ")
            user.send("JOIN #")
            user.expect(" 403 User # ")

            # +g and +s are operator-only listener modes.
            user.send("MODE User +gs")
            user.expect(" 481 User ")

            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin :You are now a Network Administrator")
            admin.send("MODE Admin +gs")
            admin.expect(" 221 Admin +")

            admin.send("GLOBOPS :global operator test")
            admin.expect(" GLOBOPS :global operator test")
            admin.send("LOCOPS :local operator test")
            admin.expect(" LOCOPS :local operator test")

            # A security action generates a +s server notice.
            admin.send("KLINE nobody@192.0.2.1 :notice policy test")
            admin.expect("NOTICE Admin :KLINE added: nobody@192.0.2.1")
            admin.expect("*** Admin added KLINE nobody@192.0.2.1")
            admin.send("KLINE -nobody@192.0.2.1")
            admin.expect("NOTICE Admin :KLINE removed: nobody@192.0.2.1")

            admin.send("JOIN #colors")
            admin.expect(" JOIN #colors")
            user.send("JOIN #colors")
            user.expect(" JOIN #colors")

            # +c rejects IRC color control codes entirely.
            admin.send("MODE #colors +c")
            admin.expect(" MODE #colors +c")
            user.send("PRIVMSG #colors :\x0304red text")
            user.expect(" 404 User #colors :Cannot send to channel")
            admin.expect_not("red text")

            # +S strips color codes before live delivery and history storage.
            admin.send("MODE #colors -c+S")
            admin.expect(" MODE #colors -c+S")
            user.send("PRIVMSG #colors :before \x0304red\x03 after")
            admin.expect("PRIVMSG #colors :before red after")
            user.send("NOTICE #colors :notice \x0400ff00green\x04 done")
            admin.expect("NOTICE #colors :notice green done")
        finally:
            for client in clients:
                client.close()
            stop(proc)


if __name__ == "__main__":
    main()
