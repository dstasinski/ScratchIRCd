#!/usr/bin/env python3
"""Focused KICK hierarchy, self-kick, and cleanup regression coverage."""

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
    raise RuntimeError("server did not listen")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick}")
    client.expect(f" 001 {nick} ")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_kick_lifecycle.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-kick-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 16\ndns_timeout_seconds = 1\n")
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
            half = IRCClient(port); clients.append(half); register(half, "Half")
            voice = IRCClient(port); clients.append(voice); register(voice, "Voice")
            oper = IRCClient(port); clients.append(oper); register(oper, "ChanOp")
            watcher = IRCClient(port); clients.append(watcher); register(watcher, "Watcher")

            owner.send("JOIN #kickrank"); owner.expect(" 366 Owner #kickrank ")
            for client, nick in ((half, "Half"), (voice, "Voice"), (oper, "ChanOp")):
                client.send("JOIN #kickrank")
                client.expect(f" 366 {nick} #kickrank ")
            owner.send("MODE #kickrank +h Half"); half.expect(" MODE #kickrank +h Half")
            owner.send("MODE #kickrank +v Voice"); voice.expect(" MODE #kickrank +v Voice")
            owner.send("MODE #kickrank +o ChanOp"); oper.expect(" MODE #kickrank +o ChanOp")

            # KICK's channel and nick tokens are raw query text until lookup or
            # validation succeeds. Maximum legal requests must still receive
            # their 403/401 errors within the IRC content envelope.
            invalid_channel = "#" + ("c" * 502)
            assert len(("KICK " + invalid_channel + " X").encode()) == 510
            owner.send("KICK " + invalid_channel + " X")
            invalid_line = owner.expect(" 403 Owner ")
            assert len(invalid_line.encode()) <= 510, invalid_line
            invalid_echo = invalid_line.split(" 403 Owner ", 1)[1].rsplit(
                " :No such channel", 1)[0]
            assert 0 < len(invalid_echo) < len(invalid_channel), invalid_line
            assert invalid_channel.startswith(invalid_echo), invalid_echo

            unknown_nick = "u" * 495
            assert len(("KICK #kickrank " + unknown_nick).encode()) == 510
            owner.send("KICK #kickrank " + unknown_nick)
            unknown_line = owner.expect(" 401 Owner ")
            assert len(unknown_line.encode()) <= 510, unknown_line
            unknown_echo = unknown_line.split(" 401 Owner ", 1)[1].rsplit(
                " :No such nick/channel", 1)[0]
            assert 0 < len(unknown_echo) < len(unknown_nick), unknown_line
            assert unknown_nick.startswith(unknown_echo), unknown_echo

            # Halfop may kick a lower-ranked voice but not an operator.
            half.send("KICK #kickrank ChanOp :too high")
            half.expect(" 484 Half #kickrank ")
            half.send("KICK #kickrank Voice :lower rank")
            voice.expect(" KICK #kickrank Voice :lower rank")
            voice.send("PART #kickrank :membership probe")
            voice.expect(" 442 Voice #kickrank ")

            # Self-KICK is explicitly permitted for any member with KICK authority.
            half.send("KICK #kickrank Half :self departure")
            half.expect(" KICK #kickrank Half :self departure")
            half.send("PART #kickrank :membership probe")
            half.expect(" 442 Half #kickrank ")

            # OWNER cannot be kicked by a lower-ranked channel operator.
            oper.send("KICK #kickrank Owner :not allowed")
            oper.expect(" 484 ChanOp #kickrank ")

            # Optional departure reasons may fit the inbound 510-byte line but
            # become too long after the server adds the public source prefix.
            # Preserve the action and replace only the unrelayable reason.
            long_reason = "x" * 480
            longkick = IRCClient(port); clients.append(longkick); register(longkick, "LongKick")
            longkick.send("JOIN #kickrank"); longkick.expect(" 366 LongKick #kickrank ")
            owner.send("KICK #kickrank LongKick :" + long_reason)
            longkick.expect(" KICK #kickrank LongKick :Kicked")
            longkick.send("PART #kickrank :membership probe")
            longkick.expect(" 442 LongKick #kickrank ")

            owner.send("JOIN #partlong"); owner.expect(" 366 Owner #partlong ")
            watcher.send("JOIN #partlong"); watcher.expect(" 366 Watcher #partlong ")
            watcher.send("PART #partlong :" + long_reason)
            owner.expect(" PART #partlong :Leaving")
            watcher.send("PART #partlong :membership probe")
            watcher.expect(" 442 Watcher #partlong ")

            quitter = IRCClient(port); clients.append(quitter); register(quitter, "Quitter")
            owner.send("JOIN #quitlong"); owner.expect(" 366 Owner #quitlong ")
            quitter.send("JOIN #quitlong"); quitter.expect(" 366 Quitter #quitlong ")
            quitter.send("QUIT :" + long_reason)
            owner.expect(" QUIT :Client quit")

            # Empty ephemeral channels are removed after an OWNER self-KICK.
            owner.send("JOIN #selfempty"); owner.expect(" 366 Owner #selfempty ")
            owner.send("KICK #selfempty Owner :close channel")
            owner.expect(" KICK #selfempty Owner :close channel")
            watcher.send("LIST")
            watcher.expect_not(" 322 Watcher #selfempty ")
            watcher.expect(" 323 Watcher :End of /LIST")

            # The deleted channel can be recreated normally and has no stale membership.
            watcher.send("JOIN #selfempty")
            watcher.expect(" 366 Watcher #selfempty ")
        finally:
            for client in clients:
                client.close()
            stop(proc)


if __name__ == "__main__":
    main()
