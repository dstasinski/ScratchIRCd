#!/usr/bin/env python3
"""Ensure STATS and variable-length replies respect IRC wire limits."""

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
        self.sock.settimeout(0.20)
        self.buffer = b""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def collect_until(self, needle, duration=5.0):
        deadline = time.monotonic() + duration
        lines = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode(errors="replace")
                lines.append(line)
                if needle in line:
                    return lines
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {lines!r}")

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


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3.0)


def assert_wire_safe(lines):
    for line in lines:
        assert len(line.encode()) <= 510, (len(line.encode()), line)


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER user 0 * :Envelope Peer")
    client.collect_until(f" 001 {nick} ")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_stats_envelope.py scratchircd scratchircd-mkpasswd")

    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-stats-envelope-") as td:
        port = free_port()
        bans_db = os.path.join(td, "bans.db")
        config = os.path.join(td, "ircd.conf")
        motd_file = os.path.join(td, "motd.txt")
        rules_file = os.path.join(td, "rules.txt")
        password_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        server_name = "s" * 63
        motd_text = "m" * 900
        rules_text = "r" * 900

        with open(motd_file, "w", encoding="utf-8") as f:
            f.write(motd_text + "\n")
        with open(rules_file, "w", encoding="utf-8") as f:
            f.write(rules_text + "\n")

        with open(config, "w", encoding="utf-8") as f:
            f.write(f"server_name = {server_name}\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 20\ndns_timeout_seconds = 1\n")
            f.write(f"motd_file = {motd_file}\nrules_file = {rules_file}\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {bans_db}\n")
            f.write(f"nickserv_db = {td}/nickserv.db\n")
            f.write(f"chanserv_db = {td}/chanserv.db\n")
            f.write(f"memoserv_db = {td}/memoserv.db\n")
            f.write(f"history_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {password_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, config], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, cwd=td)
        client = None
        peers = []
        try:
            wait_listen(port, proc)
            client = IRCClient(port)
            register(client, "alice")
            client.send("OPER root adminpass")
            client.collect_until(" 381 alice :You are now a Network Administrator")

            k_mask = ("u" * 191) + "@" + ("h" * 63)
            z_mask = "z" * 255
            geo_value = "o" * 255
            setter = "s" * 63
            reason = "r" * 255
            assert len(k_mask) == 255

            with sqlite3.connect(bans_db) as db:
                db.execute(
                    "INSERT INTO bans(type,mask,reason,set_by,expires_at) VALUES(1,?,?,?,0)",
                    (k_mask, reason, setter),
                )
                db.execute(
                    "INSERT INTO bans(type,mask,reason,set_by,expires_at) VALUES(2,?,?,?,0)",
                    (z_mask, reason, setter),
                )
                db.execute(
                    "INSERT INTO geo_bans(type,value,reason,set_by,expires_at) VALUES(4,?,?,?,0)",
                    (geo_value, reason, setter),
                )
                db.commit()

            client.send("STATS k")
            lines = client.collect_until(" 219 alice k :End of /STATS report")
            assert_wire_safe(lines)
            k_rows = [line for line in lines if " 216 alice " in line]
            assert len(k_rows) == 1, lines
            assert k_mask in k_rows[0] and setter in k_rows[0], k_rows[0]
            assert reason not in k_rows[0], k_rows[0]

            client.send("STATS z")
            lines = client.collect_until(" 219 alice z :End of /STATS report")
            assert_wire_safe(lines)
            z_rows = [line for line in lines if " :ZLINE " in line]
            assert len(z_rows) == 1, lines
            assert z_mask in z_rows[0] and setter in z_rows[0], z_rows[0]
            assert reason not in z_rows[0], z_rows[0]

            client.send("STATS g")
            lines = client.collect_until(" 219 alice g :End of /STATS report")
            assert_wire_safe(lines)
            g_rows = [line for line in lines if " :GEOBAN ORG " in line]
            assert len(g_rows) == 1, lines
            assert geo_value in g_rows[0] and setter in g_rows[0], g_rows[0]
            assert reason not in g_rows[0], g_rows[0]

            # File-backed text may contain physical lines far longer than one
            # IRC numeric. Preserve all text by chunking each logical line.
            client.send("MOTD")
            lines = client.collect_until(" 376 alice :End of /MOTD command.")
            assert_wire_safe(lines)
            motd_marker = " 372 alice :- "
            motd_parts = [line.split(motd_marker, 1)[1]
                          for line in lines if motd_marker in line]
            assert len(motd_parts) >= 2, lines
            assert "".join(motd_parts) == motd_text, motd_parts

            client.send("RULES")
            lines = client.collect_until(" 309 alice :End of RULES command.")
            assert_wire_safe(lines)
            rules_marker = " 232 alice :- "
            rules_parts = [line.split(rules_marker, 1)[1]
                           for line in lines if rules_marker in line]
            assert len(rules_parts) >= 2, lines
            assert "".join(rules_parts) == rules_text, rules_parts

            # WATCH may retain 128 nicknames. Listing many maximum-length
            # entries must split at token boundaries instead of truncating the
            # list or losing numeric 606 behind the 510-byte wire guard.
            watch_names = []
            for index in range(20):
                prefix = f"W{index:02d}"
                watch_names.append(prefix + ("w" * (31 - len(prefix))))
            for batch_index, batch in enumerate((watch_names[:12], watch_names[12:])):
                client.send("WATCH " + " ".join("+" + name for name in batch))
                marker = f"watch-add-{batch_index}"
                client.send(f"PING :{marker}")
                client.collect_until(f"PONG {server_name} ::{marker}")
            client.send("WATCH")
            client.send("PING :watch-list-done")
            lines = client.collect_until(f"PONG {server_name} ::watch-list-done")
            assert_wire_safe(lines)
            watch_rows = [line for line in lines if " 606 alice :" in line]
            assert len(watch_rows) >= 2, lines
            watch_payload = " ".join(line.split(" 606 alice :", 1)[1]
                                     for line in watch_rows)
            for name in watch_names:
                assert name in watch_payload, (name, watch_rows)
            assert any(" 607 alice :End of WATCH L" in line for line in lines), lines

            # ISON input can legally hold more online nick tokens than one 303
            # reply can carry once a maximum-length server prefix is added.
            # Preserve every online result across multiple 303 numerics.
            ison_names = []
            for index in range(14):
                prefix = f"I{index:02d}"
                nick = prefix + ("i" * (31 - len(prefix)))
                peer = IRCClient(port)
                peers.append(peer)
                register(peer, nick)
                ison_names.append(nick)
            client.send("ISON " + " ".join(ison_names))
            client.send("PING :ison-done")
            lines = client.collect_until(f"PONG {server_name} ::ison-done")
            assert_wire_safe(lines)
            ison_rows = [line for line in lines if " 303 alice :" in line]
            assert len(ison_rows) >= 2, lines
            ison_payload = " ".join(line.split(" 303 alice :", 1)[1]
                                    for line in ison_rows)
            for name in ison_names:
                assert name in ison_payload, (name, ison_rows)
        finally:
            for peer in peers:
                peer.close()
            if client is not None:
                client.close()
            stop(proc)

    print("STATS/variable-reply envelope integration tests passed")


if __name__ == "__main__":
    main()
