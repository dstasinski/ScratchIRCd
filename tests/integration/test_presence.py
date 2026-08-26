#!/usr/bin/env python3
"""Integration coverage for WATCH, ISON, USERHOST/USERIP, NAMES, and WHOWAS."""

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

    def _pump(self):
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            self.pending.append(raw.rstrip(b"\r").decode(errors="replace"))

    def expect(self, needle, duration=4.0):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
            self._pump()
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

    def expect_not(self, needle, duration=0.5):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
            self._pump()
            for line in self.pending:
                if needle in line:
                    raise AssertionError(f"unexpected {needle!r}; got {seen + self.pending!r}")
            seen.extend(self.pending)
            self.pending.clear()
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass

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
    client.send(f"USER {nick.lower()} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


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
            f.write("cloak_prefix = dru\n")
            f.write("cloak_key = presence-test-cloak-key-0123456789abcdef\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\n")
            f.write(f"chanserv_db = {td}/chanserv.db\n")
            f.write(f"memoserv_db = {td}/memoserv.db\n")
            f.write(f"history_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        watcher = subject = temp = pending = None
        try:
            wait_listen(port, proc)
            watcher = IRCClient(port)
            register(watcher, "Watcher")

            watcher.send("WATCH +123bad +Bad,Nick +Subject +Renamed +Temp")
            watcher.expect(" 605 Watcher Subject ")
            watcher.expect(" 605 Watcher Renamed ")
            watcher.expect(" 605 Watcher Temp ")
            watcher.send("WATCH")
            watch_list = watcher.expect(" 606 Watcher :")
            assert "123bad" not in watch_list and "Bad,Nick" not in watch_list, watch_list
            watcher.expect(" 607 Watcher :End of WATCH L")

            subject = IRCClient(port)
            register(subject, "Subject")
            watcher.expect(" 600 Watcher Subject ")

            pending = IRCClient(port)
            pending.send("NICK Pending")
            watcher.send("WATCH +Pending")
            watcher.expect(" 605 Watcher Pending * * ")
            watcher.send("WATCH -Pending")
            watcher.expect(" 602 Watcher Pending * * ")
            watcher.send("LUSERS")
            watcher.expect(" 253 Watcher 1 :unknown connection(s)")
            pending.close()
            pending = None

            subject.send("MODE Subject +x")
            subject.expect(" 221 Subject ")
            watcher.send("USERHOST Subject")
            userhost = watcher.expect(" 302 Watcher :Subject=+subject@dru-")
            assert "127.0.0.1" not in userhost and "@localhost" not in userhost, userhost

            watcher.send("USERIP Subject")
            watcher.expect(" 481 Watcher ")

            watcher.send("ISON Subject NickServ ChanServ MemoServ")
            ison = watcher.expect(" 303 Watcher :Subject")
            assert "NickServ" not in ison and "ChanServ" not in ison and "MemoServ" not in ison, ison

            subject.send("JOIN #presence")
            subject.expect(" JOIN #presence")
            watcher.send("NAMES #presence")
            names = watcher.expect(" 353 Watcher ")
            assert "Subject" in names, names
            assert "NickServ" not in names and "ChanServ" not in names and "MemoServ" not in names, names
            watcher.expect(" 366 Watcher #presence :End of /NAMES list.")

            subject.send("NICK Renamed")
            watcher.expect(" 601 Watcher Subject ")
            watcher.expect(" 600 Watcher Renamed ")
            watcher.send("WHOWAS Subject")
            historical = watcher.expect(" 314 Watcher Subject subject ")
            assert "127.0.0.1" not in historical, historical

            temp = IRCClient(port)
            register(temp, "Temp")
            watcher.expect(" 600 Watcher Temp ")
            temp.send("QUIT :gone")
            watcher.expect(" 601 Watcher Temp ")
            temp.close(); temp = None
        finally:
            for client in (watcher, subject, temp, pending):
                if client is not None:
                    client.close()
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3.0)
                except subprocess.TimeoutExpired:
                    proc.kill(); proc.wait(timeout=3.0)


if __name__ == "__main__":
    main()
