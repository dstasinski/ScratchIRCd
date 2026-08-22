#!/usr/bin/env python3
"""End-to-end coverage for SILENCE, WATCH, and WHOWAS runtime state."""

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

    def _lines(self):
        out = self.pending
        self.pending = []
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            out.append(raw.rstrip(b"\r").decode(errors="replace"))
        return out

    def expect(self, needle, duration=5.0):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            lines = self._lines()
            for index, line in enumerate(lines):
                got.append(line)
                if needle in line:
                    self.pending.extend(lines[index + 1:])
                    return line
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {got!r}")

    def expect_not(self, needle, duration=0.75):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            lines = self._lines()
            for line in lines:
                got.append(line)
                if needle in line:
                    raise AssertionError(f"unexpected {needle!r}; got {got!r}")
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
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
    client.send(f"USER {nick.lower()} 0 * :{nick} Real Name")
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
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_presence.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-presence-") as td:
        port = free_port()
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

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        watcher = subject = temp = None
        try:
            wait_listen(port, proc)
            watcher = IRCClient(port)
            register(watcher, "Watcher")

            watcher.send("WATCH +Subject +Renamed +Temp")
            watcher.expect(" 605 Watcher Subject ")
            watcher.expect(" 605 Watcher Renamed ")
            watcher.expect(" 605 Watcher Temp ")

            subject = IRCClient(port)
            register(subject, "Subject")
            watcher.expect(" 600 Watcher Subject ")

            # Nick change produces an offline event for the old watched nick,
            # an online event for the new watched nick, and a WHOWAS record.
            subject.send("NICK Renamed")
            watcher.expect(" 601 Watcher Subject ")
            watcher.expect(" 600 Watcher Renamed ")
            watcher.send("WHOWAS Subject")
            watcher.expect(" 314 Watcher Subject subject ")
            watcher.expect(" 369 Watcher Subject :End of WHOWAS")

            # SILENCE uses the sender's public nick!user@display_host identity.
            subject.send("SILENCE +Watcher!*@*")
            subject.send("SILENCE")
            subject.expect(" 271 Renamed Renamed Watcher!*@*")
            subject.expect(" 272 Renamed :End of Silence List")
            watcher.send("PRIVMSG Renamed :this must be blocked")
            subject.expect_not("this must be blocked")
            watcher.send("NOTICE Renamed :this notice must also be blocked")
            subject.expect_not("this notice must also be blocked")

            # Commands on different client sockets have no cross-connection
            # ordering guarantee. Query the list after removal and wait for 272
            # so the server has definitely processed the removal before Watcher
            # sends the delivery-restored PRIVMSG.
            subject.send("SILENCE -Watcher!*@*")
            subject.send("SILENCE")
            subject.expect(" 272 Renamed :End of Silence List")
            watcher.send("PRIVMSG Renamed :delivery restored")
            subject.expect("PRIVMSG Renamed :delivery restored")

            # Abrupt socket loss must still drive WATCH and WHOWAS through the
            # client destruction hook, not only through a clean QUIT command.
            temp = IRCClient(port)
            register(temp, "Temp")
            watcher.expect(" 600 Watcher Temp ")
            temp.close()
            temp = None
            watcher.expect(" 601 Watcher Temp ")
            watcher.send("WHOWAS Temp")
            watcher.expect(" 314 Watcher Temp temp ")

            watcher.send("WATCH")
            watcher.expect(" 606 Watcher :")
            watcher.expect(" 607 Watcher :End of WATCH L")
        finally:
            if watcher is not None:
                watcher.close()
            if subject is not None:
                subject.close()
            if temp is not None:
                temp.close()
            stop(proc)


if __name__ == "__main__":
    main()
