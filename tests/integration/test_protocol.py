#!/usr/bin/env python3
"""End-to-end ScratchIRCd protocol integration test over real TCP sockets."""

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

    def _extract_lines(self):
        out = []
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            out.append(raw.rstrip(b"\r").decode(errors="replace"))
        return out

    def lines(self, duration=0.5):
        end = time.monotonic() + duration
        out = self._extract_lines()
        while time.monotonic() < end:
            remaining = end - time.monotonic()
            self.sock.settimeout(min(0.2, max(remaining, 0.01)))
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
                out.extend(self._extract_lines())
            except socket.timeout:
                pass
        return out

    def expect(self, needle, duration=2.0):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            complete = self._extract_lines()
            got.extend(complete)
            if any(needle in line for line in complete):
                return got
            remaining = deadline - time.monotonic()
            self.sock.settimeout(min(0.2, max(remaining, 0.01)))
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        got.extend(self._extract_lines())
        if any(needle in line for line in got):
            return got
        raise AssertionError(f"expected {needle!r}; got {got!r}")

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
            stderr = proc.stderr.read() if proc.stderr else ""
            raise RuntimeError(f"scratchircd exited before listening: {stderr!r}")
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            s.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("scratchircd did not begin listening")


def write_config(path, port, motd, rules, password="", oper_hash=""):
    with open(path, "w", encoding="utf-8") as f:
        f.write("server_name = test.local\nnetwork_name = TestNet\n")
        f.write("bind_address = 127.0.0.1\n")
        f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
        f.write(f"server_password = {password}\n")
        f.write(f"motd_file = {motd}\nrules_file = {rules}\n")
        f.write("admin_location1 = Test Operations\nadmin_location2 = Test Lab\n")
        f.write("admin_email = admin@example.test\n")
        if oper_hash:
            f.write("oper_name = root\n")
            f.write(f"oper_password_hash = {oper_hash}\n")
            f.write("oper_hostmask = *!*@127.0.0.1\n")
            f.write("oper_flags = oper,can_rehash,can_kill,can_kline,can_unkline,can_zline,can_override,get_host,helpop,can_wallops,netadmin\n")
            f.write("oper_vhost = staff.test.local\n")


def register(client, nick, password=None):
    if password is not None:
        client.send(f"PASS {password}")
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")
    client.expect(f" 001 {nick} ", 3.0)


def stop_server(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)


def run_unprotected(binary, mkpasswd, tempdir):
    port = free_port()
    motd = os.path.join(tempdir, "motd.txt")
    rules = os.path.join(tempdir, "rules.txt")
    config = os.path.join(tempdir, "ircd.conf")
    with open(motd, "w", encoding="utf-8") as f:
        f.write("Welcome integration test\nSecond MOTD line\n")
    with open(rules, "w", encoding="utf-8") as f:
        f.write("Rule one\nRule two\n")

    oper_hash = subprocess.check_output([mkpasswd, "operpass"], text=True).strip()
    assert oper_hash.startswith("$argon2id$")
    write_config(config, port, motd, rules, oper_hash=oper_hash)

    proc = subprocess.Popen([binary, config], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    clients = []
    try:
        wait_listen(port, proc)
        a = IRCClient("127.0.0.1", port); clients.append(a)
        b = IRCClient("127.0.0.1", port); clients.append(b)
        register(a, "alice")
        register(b, "bob")

        a.send("OPER root wrong")
        a.expect(" 464 alice ")
        a.send("OPER root operpass")
        a.expect(" 381 alice :You are now a Network Administrator")
        a.send("MODE alice")
        modes = a.expect(" 221 alice ")
        assert any("o" in line and "N" in line and "h" in line and "t" in line
                   for line in modes), modes
        b.send("WHOIS alice")
        whois = b.expect(" 318 bob alice ")
        assert any(" 313 bob alice :is an Administrator" in line for line in whois), whois
        assert any("staff.test.local" in line for line in whois), whois

        a.send("JOIN #test"); a.expect(" JOIN #test")
        b.send("JOIN #test"); b.expect(" JOIN #test")
        a.send("TOPIC #test :Integration topic")
        b.expect(" TOPIC #test :Integration topic")
        a.send("MODE #test +m"); b.lines(0.1)
        b.send("PRIVMSG #test :blocked"); b.expect(" 404 bob #test ")
        a.send("MODE #test +v bob"); b.expect(" MODE #test +v bob")
        b.send("PRIVMSG #test :allowed"); a.expect("PRIVMSG #test :allowed")

        a.send("AWAY :testing away"); a.expect(" 306 alice ")
        b.send("PRIVMSG alice :hello"); b.expect(" 301 bob alice :testing away")
        a.send("AWAY"); a.expect(" 305 alice ")
        b.send("ISON alice bob nobody")
        got = b.expect(" 303 bob :")
        assert any("alice" in line and "bob" in line and "nobody" not in line for line in got), got

        b.send("USERHOST alice"); b.expect(" 302 bob :alice=")
        b.send("USERIP alice"); b.expect(" 340 bob :alice=")
        b.send("LUSERS"); b.expect(" 255 bob ")
        b.send("NAMES #test"); b.expect(" 353 bob = #test :")
        b.send("LIST"); b.expect(" 322 bob #test ")
        b.send("MOTD"); b.expect(" 372 bob :- Welcome integration test")
        b.send("RULES"); b.expect(" 232 bob :- Rule one")
        b.send("ADMIN"); b.expect(" 259 bob :admin@example.test")
    finally:
        for client in clients:
            client.close()
        stop_server(proc)


def run_protected(binary, tempdir):
    port = free_port()
    motd = os.path.join(tempdir, "motd2.txt")
    rules = os.path.join(tempdir, "rules2.txt")
    config = os.path.join(tempdir, "protected.conf")
    open(motd, "w", encoding="utf-8").write("Protected\n")
    open(rules, "w", encoding="utf-8").write("Protected rule\n")
    write_config(config, port, motd, rules, "secret")

    proc = subprocess.Popen([binary, config], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    c = None
    try:
        wait_listen(port, proc)
        c = IRCClient("127.0.0.1", port)
        c.send("NICK locked"); c.send("USER locked 0 * :Locked User")
        lines = c.lines(0.4)
        assert not any(" 001 locked " in line for line in lines), lines
        c.send("PASS wrong"); c.expect(" 464 locked ")
        c.send("PASS secret"); c.expect(" 001 locked ", 2.0)
    finally:
        if c is not None:
            c.close()
        stop_server(proc)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_protocol.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="scratchircd-test-") as tempdir:
        run_unprotected(binary, mkpasswd, tempdir)
        run_protected(binary, tempdir)
    print("protocol integration tests passed")


if __name__ == "__main__":
    main()
