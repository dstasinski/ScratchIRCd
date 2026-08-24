#!/usr/bin/env python3
"""End-to-end NickServ accounts, recovery, vhosts and email-reset coverage."""

import os
import re
import socket
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

    def expect_closed(self, duration=3.0):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
                if not data:
                    return
            except socket.timeout:
                pass
            except OSError:
                return
        raise AssertionError("expected connection to close")

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
    client.send(f"USER {nick} 0 * :{nick}")
    client.expect(f" 001 {nick} ")


def assert_current_nick(client, nick):
    client.send(f"MODE {nick}")
    client.expect(f" 221 {nick} ")


def wait_mail_token(mailbox, marker, start=0, duration=5.0):
    pattern = re.compile(rf"{re.escape(marker)}: ([0-9a-f]{{32}})")
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        if os.path.exists(mailbox):
            with open(mailbox, "r", encoding="utf-8", errors="replace") as f:
                f.seek(start)
                text = f.read()
            match = pattern.search(text)
            if match:
                return match.group(1), os.path.getsize(mailbox)
        time.sleep(0.05)
    raise AssertionError(f"did not receive mail containing {marker!r}")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_nickserv.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-nickserv-") as td:
        port = free_port()
        admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        mailbox = os.path.join(td, "mailbox.txt")
        sendmail = os.path.join(td, "fake-sendmail")
        with open(sendmail, "w", encoding="utf-8") as f:
            f.write("#!/bin/sh\n")
            f.write(f"cat >> {mailbox!r}\n")
            f.write(f"printf '\\n---END---\\n' >> {mailbox!r}\n")
        os.chmod(sendmail, 0o755)

        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 64\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
            f.write(f"sendmail_path = {sendmail}\n")
            f.write("mail_from = services@test.local\n")
            f.write("nickserv_reset_seconds = 60\n")
            f.write("nickserv_verify_seconds = 60\n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@*\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)

            alice = IRCClient(port); clients.append(alice)
            register(alice, "Alice")
            alice.send("NICKSERV REGISTER firstpass")
            alice.expect("Nickname registered and identified.")
            alice.send("MODE Alice")
            modes = alice.expect(" 221 Alice ")
            assert any("r" in line.rsplit(" ", 1)[-1]
                       for line in modes if " 221 Alice " in line), modes

            alice.send("ISON NickServ Alice")
            ison = alice.expect(" 303 Alice ")
            ison_line = next(line for line in ison if " 303 Alice " in line)
            assert "Alice" in ison_line and "NickServ" not in ison_line, ison

            impostor = IRCClient(port); clients.append(impostor)
            impostor.send("NICK NickServ")
            impostor.expect(" 437 ")
            impostor.close(); clients.remove(impostor)

            alice.send("NICKSERV SET EMAIL alice@example.test")
            alice.expect("Verification email queued.")
            verify_token, mail_offset = wait_mail_token(mailbox, "Verification token")
            alice.send(f"NICKSERV VERIFY {verify_token}")
            alice.expect("Email address verified.")

            # Account identity follows the connection, not the current nickname.
            alice.send("NICK Owner")
            assert_current_nick(alice, "Owner")
            alice.send("WHOIS Owner")
            owner_whois = alice.expect(" 318 Owner Owner ")
            assert any("is logged in as Alice" in line for line in owner_whois), owner_whois

            # Default RECOVER safely renames the squatter rather than disconnecting it.
            # WATCH/WHOWAS must follow the same lifecycle as an ordinary NICK, and
            # channel membership/authority must stay with the existing connection.
            watcher = IRCClient(port); clients.append(watcher)
            register(watcher, "Watcher")
            watcher.send("WATCH +Alice")
            watcher.expect(" 605 Watcher Alice ")

            squatter = IRCClient(port); clients.append(squatter)
            register(squatter, "Alice")
            watcher.expect(" 600 Watcher Alice ")
            squatter.send("JOIN #recover")
            squatter.expect(" 366 Alice #recover ")

            alice.send("PRIVMSG NickServ :RECOVER Alice")
            alice.expect("previous user was safely renamed")
            recovered_notice = squatter.expect("Your nickname was recovered")
            notice_line = next(line for line in recovered_notice if "Your nickname was recovered" in line)
            match = re.search(r"you are now (Guest[0-9]+)\.", notice_line)
            assert match, notice_line
            guest_nick = match.group(1)
            watcher.expect(" 601 Watcher Alice ")
            watcher.send("WHOWAS Alice")
            watcher.expect(" 314 Watcher Alice Alice ")
            watcher.expect(" 369 Watcher Alice :End of WHOWAS")

            squatter.send("MODE #recover +m")
            mode_lines = squatter.expect(f":{guest_nick}!Alice@")
            assert any(" MODE #recover +m" in line for line in mode_lines), mode_lines

            alice.send("NICK Alice")
            assert_current_nick(alice, "Alice")

            # RECOVER KILL disconnects the occupying session.
            alice.send("NICK Owner2")
            assert_current_nick(alice, "Owner2")
            squatter2 = IRCClient(port); clients.append(squatter2)
            register(squatter2, "Alice")
            alice.send("NICKSERV RECOVER Alice KILL")
            alice.expect("Nickname session disconnected.")
            squatter2.expect_closed()
            squatter2.close(); clients.remove(squatter2)

            # GHOST is deliberately a KILL alias.
            squatter3 = IRCClient(port); clients.append(squatter3)
            register(squatter3, "Alice")
            alice.send("NICKSERV GHOST Alice")
            alice.expect("Nickname session disconnected.")
            squatter3.expect_closed()
            squatter3.close(); clients.remove(squatter3)

            alice.send("NICKSERV SET PASSWORD secondpass")
            alice.expect("Password changed.")
            alice.send("QUIT :email reset test")
            alice.close(); clients.remove(alice)

            requester = IRCClient(port); clients.append(requester)
            register(requester, "Requester")
            requester.send("NICKSERV RESET Alice")
            requester.expect("If that account exists and has a verified email address")
            reset_token, mail_offset = wait_mail_token(mailbox, "Reset token", mail_offset)
            requester.send(f"NICKSERV RESET Alice {reset_token} thirdpass")
            requester.expect("Password reset complete.")
            requester.send("IDENTIFY Alice secondpass")
            requester.expect("Password incorrect or account unavailable.")
            requester.send("IDENTIFY Alice thirdpass")
            requester.expect("Password accepted - you are now identified.")
            requester.send("WHOIS Requester")
            whois = requester.expect(" 318 Requester Requester ")
            assert any("is logged in as Alice" in line for line in whois), whois

            admin = IRCClient(port); clients.append(admin)
            register(admin, "Admin")
            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin ")
            admin.send("NSINFO Alice")
            info = admin.expect("email=alice@example.test")
            assert any("email_verified=1" in line for line in info), info
            admin.send("NSSET Alice VHOST registered.example.test")
            admin.expect("NickServ account updated.")

            # VHOST is applied on successful authentication, even when the client
            # is already joined.  Everything that consumes public identity must
            # switch immediately from the cloak to the vhost, while channel
            # membership itself remains unchanged.
            requester.send("QUIT :replace with live vhost test")
            requester.close(); clients.remove(requester)
            user = IRCClient(port); clients.append(user)
            register(user, "Traveler")
            user.send("MODE Traveler +x")
            user.expect(" 221 Traveler ")
            admin.send("JOIN #vhost")
            admin.expect(" 366 Admin #vhost ")
            user.send("JOIN #vhost")
            user.expect(" 366 Traveler #vhost ")
            admin.send("MODE #vhost +b Traveler!*@cloak-*")
            admin.expect(" MODE #vhost +b Traveler!*@cloak-*")
            user.send("PART #vhost :pre-identify ban check")
            user.expect(" PART #vhost :pre-identify ban check")
            user.send("JOIN #vhost")
            user.expect(" 474 Traveler #vhost ")

            user.send("IDENTIFY Alice thirdpass")
            user.expect("Password accepted - you are now identified.")
            user.send("JOIN #vhost")
            user.expect(" 366 Traveler #vhost ")
            user.send("WHOIS Traveler")
            whois = user.expect(" 318 Traveler Traveler ")
            assert any(" 311 Traveler Traveler " in line and
                       " registered.example.test " in line for line in whois), whois
            user.send("PRIVMSG #vhost :vhost-prefix")
            prefixed = admin.expect("PRIVMSG #vhost :vhost-prefix")
            assert any(line.startswith(":Traveler!Traveler@registered.example.test ")
                       for line in prefixed if "PRIVMSG #vhost :vhost-prefix" in line), prefixed

            admin.send("NSSET Alice ENABLED 0")
            admin.expect("NickServ account updated.")
            fresh = IRCClient(port); clients.append(fresh)
            register(fresh, "Fresh")
            fresh.send("IDENTIFY Alice thirdpass")
            fresh.expect("Password incorrect or account unavailable.")
            admin.send("NSSET Alice ENABLED 1")
            admin.expect("NickServ account updated.")
            admin.send("NSDROP Alice")
            admin.expect("NickServ account deleted.")
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
