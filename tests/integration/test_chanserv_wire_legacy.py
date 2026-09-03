#!/usr/bin/env python3
"""ChanServ wire-envelope and legacy persistent-topic regression coverage."""

import os
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time


class IRCClient:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
        self.sock.settimeout(0.25)
        self.buffer = b""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def expect(self, needle, duration=5.0):
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

    def collect_for(self, duration=0.8):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                got.append(raw.rstrip(b"\r").decode(errors="replace"))
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            got.append(raw.rstrip(b"\r").decode(errors="replace"))
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


def irc_fold(text):
    table = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ{}|~", "abcdefghijklmnopqrstuvwxyz[]\\^")
    return text.translate(table)


def irc_nocase(left, right):
    a = irc_fold(left)
    b = irc_fold(right)
    return (a > b) - (a < b)


def open_chanserv_db(path):
    db = sqlite3.connect(path)
    db.create_collation("IRCNOCASE", irc_nocase)
    return db


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_chanserv_wire_legacy.py scratchircd")

    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.join(os.path.dirname(binary), "scratchircd-mkpasswd")
    admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
    server_name = "s" * 63
    founder = "F" * 15
    channel = "#" + "c" * 31
    description = "D" * 255
    legacy_topic = "T" * 379
    malformed_channel = "#" + "x" * 32
    clipped_alias = malformed_channel[:32]

    with tempfile.TemporaryDirectory(prefix="scratchircd-chanserv-wire-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        chanserv_db = os.path.join(td, "chanserv.db")
        with open(conf, "w", encoding="utf-8") as f:
            f.write(f"server_name = {server_name}\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 16\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\n")
            f.write(f"chanserv_db = {chanserv_db}\n")
            f.write(f"memoserv_db = {td}/memoserv.db\n")
            f.write(f"history_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        client = None
        try:
            wait_listen(port, proc)
            client = IRCClient(port)
            register(client, founder)
            client.send("NICKSERV REGISTER founderpass")
            client.expect("Nickname registered and identified.")
            client.send("OPER root adminpass")
            client.expect(f" 381 {founder} :You are now a Network Administrator")
            client.send(f"JOIN {channel}")
            client.expect(f" 366 {founder} {channel} ")
            client.send(f"CHANSERV REGISTER {channel} :{description}")
            client.expect("Channel registered successfully.")

            client.send(f"CHANSERV INFO {channel}")
            info_lines = client.collect_for()
            prefix = f":ChanServ!service@{server_name} NOTICE {founder} :"
            payloads = [line[len(prefix):] for line in info_lines
                        if line.startswith(prefix)]
            assert len(payloads) >= 2, info_lines
            assert description in "".join(payloads), payloads
            assert all(len(line.encode()) <= 510 for line in info_lines), info_lines

            client.send(f"CSINFO {channel}")
            admin_info_lines = client.collect_for()
            admin_prefix = f":{server_name} NOTICE {founder} :"
            admin_payloads = [line[len(admin_prefix):] for line in admin_info_lines
                              if line.startswith(admin_prefix)]
            assert len(admin_payloads) >= 2, admin_info_lines
            admin_joined = "".join(admin_payloads)
            assert f"CHANSERV {channel} founder={founder} enabled=1 description=" in admin_joined, admin_payloads
            assert description in admin_joined, admin_payloads
            assert " created=" in admin_joined and " updated=" in admin_joined, admin_payloads
            assert all(len(line.encode()) <= 510 for line in admin_info_lines), admin_info_lines
        finally:
            if client is not None:
                client.close()
            stop(proc)

        with open_chanserv_db(chanserv_db) as db:
            changed = db.execute(
                "UPDATE channels SET topic=?,topic_setter='legacy',topic_time=123 "
                "WHERE name=?",
                (legacy_topic, channel),
            ).rowcount
            db.execute(
                "INSERT INTO channels(name,founder,description,enabled) VALUES(?,?,?,1)",
                (malformed_channel, founder, "malformed name"),
            )
            db.commit()
            assert changed == 1

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        client = None
        try:
            wait_listen(port, proc)
            client = IRCClient(port)
            register(client, founder)
            startup = client.expect(f"PCHANNELS={channel}")
            pchannel_lines = [line for line in startup
                              if f" 005 {founder} " in line and "PCHANNELS=" in line]
            assert pchannel_lines, startup
            pchannels = " ".join(pchannel_lines)
            assert channel in pchannels, pchannel_lines
            assert malformed_channel not in pchannels, pchannel_lines
            assert clipped_alias not in pchannels, pchannel_lines

            client.send("IDENTIFY " + founder + " founderpass")
            client.expect("Password accepted - you are now identified.")
            client.send(f"JOIN {channel}")
            join_lines = client.expect(f" 366 {founder} {channel} ")
            assert not any(legacy_topic in line for line in join_lines), join_lines
            client.send(f"TOPIC {channel}")
            client.expect(f" 331 {founder} {channel} ")

            with open_chanserv_db(chanserv_db) as db:
                stored = db.execute(
                    "SELECT topic FROM channels WHERE name=?", (channel,)
                ).fetchone()[0]
                malformed_stored = db.execute(
                    "SELECT name FROM channels WHERE name=?", (malformed_channel,)
                ).fetchone()[0]
            assert stored == legacy_topic, len(stored)
            assert malformed_stored == malformed_channel
        finally:
            if client is not None:
                client.close()
            stop(proc)

        stderr = proc.stderr.read() if proc.stderr is not None else ""
        assert "persistent topic" in stderr and "live topic left unset" in stderr, stderr
        assert "379 bytes" in stderr, stderr
        assert "PCHANNELS" in stderr and "malformed" in stderr, stderr

    print("ChanServ legacy wire boundary integration tests passed")


if __name__ == "__main__":
    main()
