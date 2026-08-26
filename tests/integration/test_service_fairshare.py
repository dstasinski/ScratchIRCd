#!/usr/bin/env python3
"""End-to-end service resource fair-share limits."""

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


def register_irc(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick}")
    client.expect(f" 001 {nick} ")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_service_fairshare.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-fairshare-") as td:
        port = free_port()
        mailbox = os.path.join(td, "mailbox.txt")
        sendmail = os.path.join(td, "fake-sendmail")
        with open(sendmail, "w", encoding="utf-8") as f:
            f.write("#!/bin/sh\n")
            f.write(f"cat >> {mailbox!r}\n")
        os.chmod(sendmail, 0o755)

        nickserv_db = os.path.join(td, "nickserv.db")
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {nickserv_db}\n")
            f.write(f"chanserv_db = {td}/chanserv.db\n")
            f.write(f"memoserv_db = {td}/memoserv.db\n")
            f.write(f"history_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
            f.write("nickserv_registrations_per_ip = 1\n")
            f.write("nickserv_registration_window_seconds = 3600\n")
            f.write("nickserv_mail_requests_per_ip = 1\n")
            f.write("nickserv_mail_window_seconds = 3600\n")
            f.write("argon2_ops_per_ip = 2\n")
            f.write("argon2_window_seconds = 3600\n")
            f.write("argon2_global_ops_per_minute = 100\n")
            f.write("argon2_global_burst_per_second = 100\n")
            f.write("chanserv_max_channels_per_account = 1\n")
            f.write("memoserv_quota = 10\n")
            f.write("memoserv_sender_quota = 1\n")
            f.write("memoserv_retention_days = 90\n")
            f.write(f"sendmail_path = {sendmail}\n")
            f.write("mail_from = services@test.local\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)

            owner = IRCClient(port); clients.append(owner)
            register_irc(owner, "Owner")
            owner.send("NICKSERV REGISTER secret1")
            owner.expect("Nickname registered and identified.")

            # The IP creation quota survives disconnects/new sessions because it
            # is server-scoped, while remaining memory-only.
            second = IRCClient(port); clients.append(second)
            register_irc(second, "Second")
            second.send("NICKSERV REGISTER secret2")
            second.expect("Registration rate limit reached for your IP address")

            # REGISTER consumed one Argon2 work unit. One password change is
            # allowed; changing service syntax cannot obtain a third work unit.
            owner.send("NICKSERV SET PASSWORD secret2")
            owner.expect("Password changed.")
            owner.send("PRIVMSG NickServ :SET PASSWORD secret3")
            owner.expect("Password hashing rate limit reached")

            # One founder cannot consume the whole registered-channel namespace.
            owner.send("JOIN #one")
            owner.expect(" 366 Owner #one ")
            owner.send("CHANSERV REGISTER #one :first")
            owner.expect("Channel registered successfully.")
            owner.send("JOIN #two")
            owner.expect(" 366 Owner #two ")
            owner.send("CHANSERV REGISTER #two :second")
            owner.expect("maximum of 1 registered channels")

            # SET EMAIL consumes the one mail-producing request. The RESET is
            # sent via PRIVMSG to prove the alias cannot bypass the same limit.
            owner.send("NICKSERV SET EMAIL owner@example.test")
            owner.expect("Verification email queued.")
            owner.send("PRIVMSG NickServ :RESET Owner")
            owner.expect("Email request rate limit reached")

            # Seed an enabled recipient account. Password verification is not
            # involved in MemoServ recipient lookup, and Owner remains the
            # authenticated sender created through the normal NickServ path.
            deadline = time.monotonic() + 3.0
            while not os.path.exists(nickserv_db) and time.monotonic() < deadline:
                time.sleep(0.05)
            db = sqlite3.connect(nickserv_db)
            db.execute(
                "INSERT INTO nickserv_accounts(name,password_hash,enabled) VALUES(?,?,1)",
                ("Recipient", "$argon2id$seed"),
            )
            db.commit()
            db.close()

            owner.send("MEMOSERV SEND Recipient :first memo")
            owner.expect("Memo #1 sent to Recipient.")
            owner.send("PRIVMSG MemoServ :SEND Recipient :second memo")
            owner.expect("outstanding sent-memo limit of 1")

            # A service reached through PRIVMSG must consume the same weighted
            # database-work budget as the direct service command. Ordinary
            # PRIVMSG remains outside this smaller expensive-command bucket.
            budget = IRCClient(port); clients.append(budget)
            register_irc(budget, "Budget")
            for _ in range(12):
                budget.send("PRIVMSG NickServ :HELP")
            budget.expect(" 263 Budget NICKSERV :Please wait before repeating this command")
        finally:
            for client in clients:
                client.close()
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3.0)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=3.0)


if __name__ == "__main__":
    main()
