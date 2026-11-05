#!/usr/bin/env python3
"""End-to-end alias delivery requires the configured NickServ account and OPER."""
import os
import socket
import subprocess
import sys
import tempfile
import time


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Peer:
    def __init__(self, port, nick):
        self.nick = nick
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.sock.settimeout(.15)
        self.buffer = b""
        self.send("NICK " + nick)
        self.send("USER " + nick + " 0 * :" + nick)
        self.expect(" 001 " + nick + " ")

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def expect(self, needle, timeout=5):
        end = time.monotonic() + timeout
        seen = []
        while time.monotonic() < end:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode(errors="replace")
                seen.append(line)
                if needle in line:
                    return line
            try:
                data = self.sock.recv(8192)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}, got {seen!r}")

    def close(self):
        self.sock.close()


def main():
    binary, mkpasswd = map(os.path.abspath, sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="scratch-alias-") as td:
        port = free_port()
        password_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        conf = os.path.join(td, "ircd.conf")

        def write_config(account="TriviaBot"):
            with open(conf, "w", encoding="utf-8") as out:
                out.write(f"server_name = test.local\nnetwork_name = TestNet\nbind_address = 127.0.0.1\nport = {port}\n")
                out.write("max_clients = 16\nnospoof = no\ndns_timeout_seconds = 1\ngeoip_city_db = \ngeoip_asn_db = \n")
                for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
                    out.write(f"{name}_db = {td}/{name}.db\n")
                out.write(f"netadmin_name = root\nnetadmin_password_hash = {password_hash}\nnetadmin_hostmask = *!*@127.0.0.1\n")
                out.write(f"alias Trivia {{\n target = TriviaBot\n account = {account}\n}}\n")

        write_config()
        proc = subprocess.Popen([binary, conf], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        peers = []
        try:
            deadline = time.monotonic() + 6
            while True:
                if proc.poll() is not None:
                    raise RuntimeError(proc.stderr.read())
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=.1):
                        break
                except OSError:
                    if time.monotonic() > deadline:
                        raise RuntimeError("server did not start")
                    time.sleep(.05)

            sender = Peer(port, "Sender"); peers.append(sender)
            sender.send("TRIVIA offline")
            sender.expect(" 440 Sender TriviaBot ")

            bot = Peer(port, "TriviaBot"); peers.append(bot)
            sender.send("TRIVIA unregistered")
            sender.expect(" 440 Sender TriviaBot ")

            bot.send("NICKSERV REGISTER secretpass")
            bot.expect("Nickname registered and identified.")
            sender.send("TRIVIA notoper")
            sender.expect(" 440 Sender TriviaBot ")

            bot.send("OPER root adminpass")
            bot.expect(" 381 TriviaBot ")
            sender.send("TRIVIA :hello from alias")
            bot.expect("PRIVMSG TriviaBot :hello from alias")

            # A successful REHASH must immediately enforce the new account binding.
            write_config("OtherAccount")
            bot.send("REHASH")
            bot.expect(" 382 TriviaBot ")
            sender.send("TRIVIA changedaccount")
            sender.expect(" 440 Sender TriviaBot ")

            write_config("TriviaBot")
            bot.send("REHASH")
            bot.expect(" 382 TriviaBot ")
            sender.send("TRIVIA :restored")
            bot.expect("PRIVMSG TriviaBot :restored")

            bot.send("QUIT :bye")
            bot.close(); peers.remove(bot)
            # QUIT is processed asynchronously. Wait until the server confirms
            # the target nickname is gone before asserting alias unavailability.
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                sender.send("ISON TriviaBot")
                ison = sender.expect(" 303 Sender ")
                if ison.rsplit(" :", 1)[-1].strip() == "":
                    break
                time.sleep(.05)
            else:
                raise AssertionError("TriviaBot remained visible after QUIT")
            sender.send("TRIVIA disconnected")
            sender.expect(" 440 Sender TriviaBot ")
        finally:
            for peer in peers:
                peer.close()
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)


if __name__ == "__main__":
    main()
