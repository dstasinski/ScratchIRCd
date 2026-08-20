#!/usr/bin/env python3
"""End-to-end OpenSSL/TLS integration coverage for ScratchIRCd."""

import os
import socket
import ssl
import subprocess
import sys
import tempfile
import time


class IRCClient:
    def __init__(self, sock):
        self.sock = sock
        self.sock.settimeout(0.25)
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
            s = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            s.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("TLS listener did not start")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_tls.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-tls-") as td:
        cert = os.path.join(td, "cert.pem")
        key = os.path.join(td, "key.pem")
        plain_port = free_port()
        tls_port = free_port()
        conf = os.path.join(td, "ircd.conf")

        subprocess.check_call([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", key, "-out", cert, "-days", "1",
            "-subj", "/CN=localhost"
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {plain_port}\ntls_port = {tls_port}\n")
            f.write(f"tls_cert_file = {cert}\ntls_key_file = {key}\n")
            f.write("max_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        tls_client = None
        plain_client = None
        try:
            wait_listen(tls_port, proc)

            context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
            raw = socket.create_connection(("127.0.0.1", tls_port), timeout=3.0)
            tls_client = IRCClient(context.wrap_socket(raw, server_hostname="localhost"))
            register(tls_client, "secure")

            tls_client.send("MODE secure")
            modes = tls_client.expect(" 221 secure ")
            assert any(line.rstrip().endswith("+z") or "z" in line.rsplit(" ", 1)[-1]
                       for line in modes if " 221 secure " in line), modes

            tls_client.send("JOIN #secure")
            tls_client.expect(" JOIN #secure")
            tls_client.send("MODE #secure +z")
            tls_client.expect(" MODE #secure +z")

            raw_plain = socket.create_connection(("127.0.0.1", plain_port), timeout=3.0)
            plain_client = IRCClient(raw_plain)
            register(plain_client, "plain")
            plain_client.send("MODE plain")
            pmodes = plain_client.expect(" 221 plain ")
            assert all("z" not in line.rsplit(" ", 1)[-1]
                       for line in pmodes if " 221 plain " in line), pmodes

            plain_client.send("JOIN #secure")
            denied = plain_client.expect(" #secure ")
            assert any(" 489 " in line or "secure" in line.lower()
                       for line in denied), denied

        finally:
            if tls_client is not None:
                tls_client.close()
            if plain_client is not None:
                plain_client.close()
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=2)

    print("TLS integration test passed")


if __name__ == "__main__":
    main()
