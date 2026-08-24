#!/usr/bin/env python3
"""End-to-end channel authority and action restriction tests."""

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

    def _next(self, deadline):
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
            line = self._next(deadline)
            if line is None:
                continue
            got.append(line)
            if needle in line:
                return line
        raise AssertionError(f"expected {needle!r}; got {got!r}")

    def expect_not(self, needle, duration=0.5):
        deadline = time.monotonic() + duration
        kept = []
        while time.monotonic() < deadline:
            line = self._next(deadline)
            if line is None:
                continue
            if needle in line:
                raise AssertionError(f"unexpected {needle!r}: {line!r}")
            kept.append(line)
        self.pending.extend(kept)

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
    raise RuntimeError("server did not listen")


def register(c, nick):
    c.send(f"NICK {nick}")
    c.send(f"USER {nick} 0 * :{nick}")
    c.expect(f" 001 {nick} ")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait(timeout=3)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_channel_authority.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-authority-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\nbans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\nchanserv_db = {td}/chanserv.db\n")
            f.write(f"memoserv_db = {td}/memoserv.db\nhistory_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)
            owner = IRCClient(port); clients.append(owner); register(owner, "Owner")
            protect = IRCClient(port); clients.append(protect); register(protect, "Protect")
            oper = IRCClient(port); clients.append(oper); register(oper, "ChanOp")
            half = IRCClient(port); clients.append(half); register(half, "Half")
            voice = IRCClient(port); clients.append(voice); register(voice, "Voice")
            outsider = IRCClient(port); clients.append(outsider); register(outsider, "Outside")
            pending = IRCClient(port); clients.append(pending); pending.send("NICK Pending")

            owner.send("JOIN #authority"); owner.expect(" 366 Owner #authority ")
            for c, nick in [(protect, "Protect"), (oper, "ChanOp"),
                            (half, "Half"), (voice, "Voice")]:
                c.send("JOIN #authority"); c.expect(f" 366 {nick} #authority ")

            owner.send("MODE #authority +v Voice"); voice.expect(" MODE #authority +v Voice")
            owner.send("MODE #authority +h Half"); half.expect(" MODE #authority +h Half")
            owner.send("MODE #authority +o ChanOp"); oper.expect(" MODE #authority +o ChanOp")
            owner.send("MODE #authority +a Protect"); protect.expect(" MODE #authority +a Protect")

            # Voice is below INVITE authority; halfop and every higher rank may invite.
            voice.send("INVITE Outside #authority")
            voice.expect(" 482 Voice #authority ")
            half.send("INVITE Outside #authority")
            half.expect(" 341 Half Outside #authority")
            outsider.expect(" INVITE Outside :#authority")
            protect.send("INVITE Outside #authority")
            protect.expect(" 341 Protect Outside #authority")
            outsider.expect(" INVITE Outside :#authority")

            # A connection that has only claimed a nickname is not a visible
            # invite target and must not learn a channel name before registration.
            half.send("INVITE Pending #authority")
            half.expect(" 401 Half Pending :No such nick/channel")
            pending.expect_not(" INVITE Pending :#authority")

            # +V blocks INVITE even for protected/owner authority. ERR_518's
            # numerics.h format places the channel in the trailing text.
            owner.send("MODE #authority +V"); protect.expect(" MODE #authority +V")
            protect.send("INVITE Outside #authority")
            protect.expect(" 518 Protect :Cannot invite (+V) at channel #authority")
            outsider.expect_not(" INVITE Outside :#authority")
            owner.send("MODE #authority -V"); protect.expect(" MODE #authority -V")

            # +t requires halfop or higher. Voice cannot set the topic; halfop can.
            owner.send("MODE #authority +t"); voice.expect(" MODE #authority +t")
            voice.send("TOPIC #authority :voice topic")
            voice.expect(" 482 Voice #authority ")
            half.send("TOPIC #authority :halfop topic")
            owner.expect(" TOPIC #authority :halfop topic")

            # +T blocks channel NOTICE; removing it restores NOTICE delivery.
            owner.send("MODE #authority +T"); protect.expect(" MODE #authority +T")
            voice.send("NOTICE #authority :blocked notice")
            protect.expect_not("NOTICE #authority :blocked notice")
            owner.send("MODE #authority -T"); protect.expect(" MODE #authority -T")
            voice.send("NOTICE #authority :restored notice")
            protect.expect("NOTICE #authority :restored notice")

            # Last-member PART removes an ephemeral channel from runtime state.
            outsider.send("JOIN #ephemeral"); outsider.expect(" 366 Outside #ephemeral ")
            outsider.send("PART #ephemeral :gone"); outsider.expect(" PART #ephemeral :gone")
            outsider.send("LIST")
            outsider.expect(" 323 Outside :End of /LIST")
            outsider.expect_not(" 322 Outside #ephemeral ")

        finally:
            for c in clients: c.close()
            stop(proc)


if __name__ == "__main__":
    main()
