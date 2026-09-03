#!/usr/bin/env python3
"""End-to-end coverage for SILENCE, WATCH, WHOWAS, and public identity visibility."""

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

    def collect_until(self, needle, duration=5.0):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            lines = self._lines()
            for index, line in enumerate(lines):
                got.append(line)
                if needle in line:
                    self.pending.extend(lines[index + 1:])
                    return got
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected terminator {needle!r}; got {got!r}")

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

            # WATCH must store only syntactically valid IRC nicknames.
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
            assert "dru-" in historical and "127.0.0.1" not in historical, historical
            watcher.expect(" 369 Watcher Subject :End of WHOWAS")

            # TOPICLEN is a server-wide wire guarantee, not merely storage size.
            # Exactly 378 bytes is advertised/accepted; 379 must be rejected
            # before mutation, and the previously stored topic must remain.
            topic_ok = "t" * 378
            topic_too_long = "x" * 379
            subject.send(f"TOPIC #presence :{topic_ok}")
            subject.expect(f" TOPIC #presence :{topic_ok}")
            watcher.send("TOPIC #presence")
            watcher.expect(f" 332 Watcher #presence :{topic_ok}")
            subject.send(f"TOPIC #presence :{topic_too_long}")
            subject.expect(" 417 Renamed TOPIC :Topic would exceed the IRC line limit")
            watcher.send("TOPIC #presence")
            topic_after_reject = watcher.expect(" 332 Watcher #presence :")
            assert topic_after_reject.endswith(topic_ok), topic_after_reject

            # A legal client can belong to enough maximum-length channel names
            # that WHOIS requires multiple 319 numerics. Every membership must
            # survive the split, with no emitted IRC line exceeding 512 bytes.
            long_channels = []
            for index in range(16):
                suffix = f"{index:02d}"
                channel = "#" + ("c" * (31 - len(suffix))) + suffix
                assert len(channel) == 32
                long_channels.append(channel)
                subject.send(f"JOIN {channel}")
                subject.expect(f" 366 Renamed {channel} ")
            watcher.send("WHOIS Renamed")
            whois_lines = watcher.collect_until(" 318 Watcher Renamed :End of /WHOIS list.")
            channel_lines = [line for line in whois_lines if " 319 Watcher Renamed :" in line]
            assert len(channel_lines) >= 2, channel_lines
            for line in channel_lines:
                assert len((line + "\r\n").encode()) <= 512, line
            joined_channels = " ".join(channel_lines)
            assert "#presence" in joined_channels, joined_channels
            for channel in long_channels:
                assert channel in joined_channels, (channel, channel_lines)

            # Control-bearing SILENCE masks are ignored and never enter list state.
            subject.send("SILENCE +bad\x01mask")
            subject.send("SILENCE +Watcher!*@*")
            subject.send("SILENCE")
            subject.expect(" 271 Renamed Renamed Watcher!*@*")
            silence_end = subject.expect(" 272 Renamed :End of Silence List")
            assert "bad" not in silence_end
            watcher.send("PRIVMSG Renamed :this must be blocked")
            subject.expect_not("this must be blocked")
            watcher.send("NOTICE Renamed :this notice must also be blocked")
            subject.expect_not("this notice must also be blocked")

            subject.send("SILENCE -Watcher!*@*")
            subject.send("SILENCE")
            subject.expect(" 272 Renamed :End of Silence List")
            watcher.send("PRIVMSG Renamed :delivery restored")
            subject.expect("PRIVMSG Renamed :delivery restored")

            watcher.send("LUSERS")
            watcher.expect(" 265 Watcher :Current Local Users: 2  Max: 2")
            temp = IRCClient(port)
            register(temp, "Temp")
            watcher.expect(" 600 Watcher Temp ")
            watcher.send("LUSERS")
            watcher.expect(" 265 Watcher :Current Local Users: 3  Max: 3")

            temp.close()
            temp = None
            watcher.expect(" 601 Watcher Temp ")
            watcher.send("LUSERS")
            watcher.expect(" 265 Watcher :Current Local Users: 2  Max: 3")
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
            if pending is not None:
                pending.close()
            stop(proc)


if __name__ == "__main__":
    main()
