#!/usr/bin/env python3
"""Fan-out regression coverage for large NAMES and shared-channel NICK/QUIT delivery."""

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

    def read_lines(self, duration=0.5):
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
            for line in self.read_lines(0.1):
                seen.append(line)
                if needle in line:
                    return line
        raise AssertionError(f"expected {needle!r}; got {seen!r}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def free_port():
    sock = socket.socket()
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
    client.send(f"USER {nick} 0 * :{nick}")
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
        raise SystemExit("usage: test_fanout.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-fanout-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as handle:
            handle.write("server_name = test.local\nnetwork_name = TestNet\n")
            handle.write("bind_address = 127.0.0.1\n")
            handle.write(f"port = {port}\nmax_clients = 64\ndns_timeout_seconds = 1\n")
            for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
                handle.write(f"{name}_db = {td}/{name}.db\n")
            handle.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)

            actor = IRCClient(port); clients.append(actor); register(actor, "Actor")
            observer = IRCClient(port); clients.append(observer); register(observer, "Observer")
            actor.send("JOIN #one"); actor.expect(" 366 Actor #one ")
            observer.send("JOIN #one"); observer.expect(" 366 Observer #one ")
            actor.send("JOIN #two"); actor.expect(" 366 Actor #two ")
            observer.send("JOIN #two"); observer.expect(" 366 Observer #two ")
            observer.read_lines(0.2)

            actor.send("NICK ActorRenamed")
            lines = observer.read_lines(0.8)
            nick_lines = [line for line in lines if " NICK :ActorRenamed" in line]
            assert len(nick_lines) == 1, nick_lines

            observer.read_lines(0.2)
            actor.send("QUIT :fanout-test")
            lines = observer.read_lines(0.8)
            quit_lines = [line for line in lines if "ActorRenamed!" in line and " QUIT :fanout-test" in line]
            assert len(quit_lines) == 1, quit_lines

            # Build a channel large enough that its complete NAMES payload cannot
            # fit on one IRC line. All members must still be returned across
            # multiple 353 replies, each within the 512-byte wire limit.
            large = []
            for index in range(18):
                nick = f"Member{index:02d}" + ("X" * 20)
                c = IRCClient(port); clients.append(c); register(c, nick)
                c.send("JOIN #large"); c.expect(f" 366 {nick} #large ")
                large.append((c, nick))

            requester = large[0][0]
            requester.read_lines(0.2)
            requester.send("NAMES #large")
            deadline = time.monotonic() + 5.0
            replies = []
            while time.monotonic() < deadline:
                replies.extend(requester.read_lines(0.1))
                if any(" 366 " in line and " #large " in line for line in replies):
                    break
            name_lines = [line for line in replies if " 353 " in line and " #large :" in line]
            assert len(name_lines) >= 2, name_lines
            for line in name_lines:
                assert len((line + "\r\n").encode()) <= 512, len((line + "\r\n").encode())
            payload = " ".join(line.split(" #large :", 1)[1] for line in name_lines)
            for _, nick in large:
                assert nick in payload, (nick, payload)
        finally:
            for client in clients:
                client.close()
            stop(proc)


if __name__ == "__main__":
    main()
