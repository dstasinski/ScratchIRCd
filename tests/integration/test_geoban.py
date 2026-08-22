#!/usr/bin/env python3
"""End-to-end GEOBAN/UNGEOBAN permission, syntax, and restart persistence tests."""

import os
import socket
import subprocess
import sys
import tempfile
import time

class IRCClient:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
        self.sock.settimeout(0.2)
        self.buffer = b""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def expect(self, needle, duration=3.0):
        deadline = time.monotonic() + duration
        got = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode(errors="replace")
                got.append(line)
                if needle in line:
                    return line
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {got!r}")

    def close(self):
        try: self.sock.close()
        except OSError: pass

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
    raise RuntimeError("server did not listen")

def register(c, nick):
    c.send(f"NICK {nick}")
    c.send(f"USER {nick} 0 * :{nick}")
    c.expect(f" 001 {nick} ")

def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try: proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill(); proc.wait(timeout=3)

def start(binary, conf, port):
    proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    wait_listen(port, proc)
    return proc

def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_geoban.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-geoban-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\nbans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\nchanserv_db = {td}/chanserv.db\n")
            f.write(f"memoserv_db = {td}/memoserv.db\nhistory_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = start(binary, conf, port)
        admin = user = None
        try:
            admin = IRCClient(port); register(admin, "Admin")
            user = IRCClient(port); register(user, "User")
            user.send("GEOBAN COUNTRY RU 0 :denied")
            user.expect(" 481 User ")

            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin :You are now a Network Administrator")
            admin.send("GEOBAN COUNTRY ru 0 :country reason")
            admin.expect("GEOBAN added: COUNTRY {RU} permanent")
            admin.send("GEOBAN ASN AS22773 1d :asn reason")
            admin.expect("GEOBAN added: ASN {22773} 1d")
            admin.send("GEOBAN ORG {*Example Network*} forever :org reason")
            admin.expect("GEOBAN added: ORG {*Example Network*} permanent")
            admin.send("GEOBAN LIST")
            admin.expect("GEOBAN COUNTRY {RU}")
            admin.send("GEOBAN LIST")
            admin.expect("GEOBAN ORG {*Example Network*}")
        finally:
            if admin: admin.close()
            if user: user.close()
            stop(proc)

        # Policies survive daemon restart.
        proc = start(binary, conf, port)
        admin = None
        try:
            admin = IRCClient(port); register(admin, "Admin2")
            admin.send("OPER root adminpass")
            admin.expect(" 381 Admin2 :You are now a Network Administrator")
            admin.send("GEOBAN LIST")
            admin.expect("GEOBAN ASN {22773}")
            admin.send("UNGEOBAN COUNTRY ru")
            admin.expect("GEOBAN removed: COUNTRY {RU}")
            admin.send("UNGEOBAN ASN 22773")
            admin.expect("GEOBAN removed: ASN {22773}")
            admin.send("UNGEOBAN ORG {*Example Network*}")
            admin.expect("GEOBAN removed: ORG {*Example Network*}")
            admin.send("GEOBAN LIST")
            admin.expect("End of GEOBAN list (0 active)")
        finally:
            if admin: admin.close()
            stop(proc)

if __name__ == "__main__":
    main()
