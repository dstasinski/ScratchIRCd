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


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_chanserv_wire_legacy.py scratchircd")

    binary = os.path.abspath(sys.argv[1])
    server_name = "s" * 63
    founder = "F" * 31
    channel = "#" + "c" * 62
    description = "D" * 255
    legacy_topic = "T" * 311

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

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        client = None
        try:
            wait_listen(port, proc)
            client = IRCClient(port)
            register(client, founder)
            client.send("NICKSERV REGISTER founderpass")
            client.expect("Nickname registered and identified.")
            client.send(f"JOIN {channel}")
            client.expect(f" 366 {founder} {channel} ")
            client.send(f"CHANSERV REGISTER {channel} :{description}")
            client.expect("Channel registered successfully.")

            # Maximal legal server/nick/channel/description fields force INFO
            # across more than one ChanServ NOTICE. Reassembly must retain the
            # full stored description and every wire line must remain <= 510.
            client.send(f"CHANSERV INFO {channel}")
            info_lines = client.collect_for()
            prefix = f":ChanServ!service@{server_name} NOTICE {founder} :"
            payloads = [line[len(prefix):] for line in info_lines
                        if line.startswith(prefix)]
            assert len(payloads) >= 2, info_lines
            assert description in "".join(payloads), payloads
            assert all(len(line.encode()) <= 510 for line in info_lines), info_lines
        finally:
            if client is not None:
                client.close()
            stop(proc)

        # Simulate a legacy/external database containing a topic that was legal
        # under the old 390-byte storage limit but exceeds today's TOPICLEN.
        with sqlite3.connect(chanserv_db) as db:
            changed = db.execute(
                "UPDATE channels SET topic=?,topic_setter='legacy',topic_time=123 "
                "WHERE name=?",
                (legacy_topic, channel),
            ).rowcount
            db.commit()
            assert changed == 1

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        client = None
        try:
            wait_listen(port, proc)
            client = IRCClient(port)
            register(client, founder)
            client.send("IDENTIFY " + founder + " founderpass")
            client.expect("Password accepted - you are now identified.")
            client.send(f"JOIN {channel}")
            join_lines = client.expect(f" 366 {founder} {channel} ")
            assert not any(legacy_topic in line for line in join_lines), join_lines
            client.send(f"TOPIC {channel}")
            client.expect(f" 331 {founder} {channel} ")

            with sqlite3.connect(chanserv_db) as db:
                stored = db.execute(
                    "SELECT topic FROM channels WHERE name=?", (channel,)
                ).fetchone()[0]
            assert stored == legacy_topic, len(stored)
        finally:
            if client is not None:
                client.close()
            stop(proc)

        stderr = proc.stderr.read() if proc.stderr is not None else ""
        assert "persistent topic" in stderr and "live topic left unset" in stderr, stderr
        assert "311 bytes" in stderr, stderr

    print("ChanServ legacy wire boundary integration tests passed")


if __name__ == "__main__":
    main()
