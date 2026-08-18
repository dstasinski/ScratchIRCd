#!/usr/bin/env python3
"""End-to-end ScratchIRCd protocol smoke/integration test.

The test launches the compiled daemon on a temporary loopback port and speaks
IRC over real TCP sockets.  It deliberately checks externally visible protocol
behavior rather than calling C internals, complementing the unit tests.
"""

import os
import socket
import subprocess
import sys
import tempfile
import time


class IRCClient:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=3.0)
        self.sock.settimeout(0.25)
        self.buffer = b""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def lines(self, duration=0.8):
        end = time.time() + duration
        out = []
        while time.time() < end:
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                out.append(raw.rstrip(b"\r").decode(errors="replace"))
        return out

    def expect(self, needle, duration=2.0):
        got = self.lines(duration)
        if not any(needle in line for line in got):
            raise AssertionError(f"expected {needle!r}; got {got!r}")
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
    deadline = time.time() + 5.0
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError("scratchircd exited before listening")
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            s.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("scratchircd did not begin listening")


def write_config(path, port, motd, rules, password=""):
    with open(path, "w", encoding="utf-8") as f:
        f.write("server_name = test.local\n")
        f.write("network_name = TestNet\n")
        f.write("bind_address = 127.0.0.1\n")
        f.write(f"port = {port}\n")
        f.write("max_clients = 32\n")
        f.write("dns_timeout_seconds = 1\n")
        f.write(f"server_password = {password}\n")
        f.write(f"motd_file = {motd}\n")
        f.write(f"rules_file = {rules}\n")
        f.write("admin_location1 = Test Operations\n")
        f.write("admin_location2 = Test Lab\n")
        f.write("admin_email = admin@example.test\n")


def register(client, nick, password=None):
    if password is not None:
        client.send(f"PASS {password}")
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")
    client.expect(f" 001 {nick} ", 3.0)


def run_unprotected(binary, tempdir):
    port = free_port()
    motd = os.path.join(tempdir, "motd.txt")
    rules = os.path.join(tempdir, "rules.txt")
    config = os.path.join(tempdir, "ircd.conf")
    open(motd, "w", encoding="utf-8").write("Welcome integration test\nSecond MOTD line\n")
    open(rules, "w", encoding="utf-8").write("Rule one\nRule two\n")
    write_config(config, port, motd, rules)

    proc = subprocess.Popen([binary, config], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    clients = []
    try:
        wait_listen(port, proc)
        a = IRCClient("127.0.0.1", port); clients.append(a)
        b = IRCClient("127.0.0.1", port); clients.append(b)
        register(a, "alice")
        register(b, "bob")

        a.send("JOIN #test"); a.expect(" JOIN #test")
        b.send("JOIN #test"); b.expect(" JOIN #test")

        a.send("TOPIC #test :Integration topic")
        b.expect(" TOPIC #test :Integration topic")
        b.send("TOPIC #test"); b.expect(" 332 bob #test :Integration topic")

        a.send("MODE #test +m")
        b.lines(0.3)
        b.send("PRIVMSG #test :blocked")
        b.expect(" 404 bob #test ")
        a.send("MODE #test +v bob")
        b.expect(" MODE #test +v bob")
        b.send("PRIVMSG #test :allowed")
        a.expect("PRIVMSG #test :allowed")

        a.send("AWAY :testing away")
        a.expect(" 306 alice ")
        b.send("PRIVMSG alice :hello")
        b.expect(" 301 bob alice :testing away")
        a.send("AWAY")
        a.expect(" 305 alice ")

        b.send("ISON alice bob nobody")
        got = b.expect(" 303 bob :")
        assert any("alice" in line and "bob" in line and "nobody" not in line for line in got)

        b.send("USERHOST alice")
        b.expect(" 302 bob :alice=")
        b.send("USERIP alice")
        b.expect(" 340 bob :alice=")
        b.send("LUSERS")
        b.expect(" 255 bob ")

        b.send("NAMES #test")
        b.expect(" 353 bob = #test :")
        b.send("LIST")
        b.expect(" 322 bob #test ")
        b.send("WHOIS alice")
        b.expect(" 318 bob alice ")

        b.send("MOTD")
        b.expect(" 372 bob :- Welcome integration test")
        b.send("RULES")
        b.expect(" 232 bob :- Rule one")
        b.send("ADMIN")
        b.expect(" 259 bob :admin@example.test")
    finally:
        for c in clients:
            c.close()
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


def run_protected(binary, tempdir):
    port = free_port()
    motd = os.path.join(tempdir, "motd2.txt")
    rules = os.path.join(tempdir, "rules2.txt")
    config = os.path.join(tempdir, "protected.conf")
    open(motd, "w", encoding="utf-8").write("Protected\n")
    open(rules, "w", encoding="utf-8").write("Protected rule\n")
    write_config(config, port, motd, rules, "secret")

    proc = subprocess.Popen([binary, config], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    c = None
    try:
        wait_listen(port, proc)
        c = IRCClient("127.0.0.1", port)
        c.send("NICK locked")
        c.send("USER locked 0 * :Locked User")
        lines = c.lines(1.5)
        assert not any(" 001 locked " in line for line in lines), lines
        c.send("PASS wrong")
        c.expect(" 464 locked ")
        c.send("PASS secret")
        c.expect(" 001 locked ", 2.0)
    finally:
        if c is not None:
            c.close()
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_protocol.py /path/to/scratchircd")
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-test-") as tempdir:
        run_unprotected(binary, tempdir)
        run_protected(binary, tempdir)
    print("protocol integration tests passed")


if __name__ == "__main__":
    main()
