#!/usr/bin/env python3
"""Ensure STATS/LIST/file-text replies respect IRC wire limits."""

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
            f.write(f"port = {port}\nmax_clients = 8\ndns_timeout_seconds = 1\n")
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
        viewer = None
        try:
            wait_listen(port, proc)
            client = IRCClient(port)
            client.send("NICK alice")
            client.send("USER alice 0 * :Alice User")
            client.collect_until(" 001 alice ")
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

            # With a 63-byte server name, irc_topic_limit() advertises 344.
            # That topic is legal for TOPIC/332, but numeric 322 has slightly
            # more fixed framing. A 31-byte requesting nick therefore forces
            # LIST to trim only the displayed trailing topic instead of losing
            # the channel row entirely.
            channel = "#" + ("c" * 62)
            topic = "t" * 344
            client.send(f"JOIN {channel}")
            client.collect_until(f" 366 alice {channel} ")
            client.send(f"TOPIC {channel} :{topic}")
            client.collect_until(f" TOPIC {channel} :")

            viewer_nick = "v" * 31
            viewer = IRCClient(port)
            viewer.send(f"NICK {viewer_nick}")
            viewer.send(f"USER viewer 0 * :List Viewer")
            viewer.collect_until(f" 001 {viewer_nick} ")
            viewer.send("LIST")
            lines = viewer.collect_until(f" 323 {viewer_nick} :End of /LIST")
            assert_wire_safe(lines)
            list_rows = [line for line in lines if f" 322 {viewer_nick} {channel} " in line]
            assert len(list_rows) == 1, lines
            displayed_topic = list_rows[0].rsplit(":", 1)[1]
            assert displayed_topic and set(displayed_topic) == {"t"}, list_rows[0]
            assert len(displayed_topic) < len(topic), list_rows[0]
        finally:
            if viewer is not None:
                viewer.close()
            if client is not None:
                client.close()
            stop(proc)

    print("STATS/LIST/file-text envelope integration tests passed")


if __name__ == "__main__":
    main()
