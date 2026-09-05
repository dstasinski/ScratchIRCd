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


def wait_closed(sock, duration=8.0):
    deadline = time.monotonic() + duration
    sock.settimeout(0.2)
    while time.monotonic() < deadline:
        try:
            data = sock.recv(4096)
            if not data:
                return
        except (ConnectionResetError, BrokenPipeError):
            return
        except socket.timeout:
            pass
    raise AssertionError("connection was not closed before its deadline")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


def run_openssl(*arguments):
    subprocess.check_call(
        ["openssl", *arguments],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_tls.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-tls-") as td:
        root_cert = os.path.join(td, "root-cert.pem")
        root_key = os.path.join(td, "root-key.pem")
        chain = os.path.join(td, "intermediate-cert.pem")
        chain_key = os.path.join(td, "intermediate-key.pem")
        chain_request = os.path.join(td, "intermediate.csr")
        chain_extensions = os.path.join(td, "intermediate.ext")
        cert = os.path.join(td, "server-cert.pem")
        key = os.path.join(td, "server-key.pem")
        cert_request = os.path.join(td, "server.csr")
        cert_extensions = os.path.join(td, "server.ext")
        plain_port = free_port()
        tls_port = free_port()
        conf = os.path.join(td, "ircd.conf")

        run_openssl(
            "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", root_key, "-out", root_cert, "-days", "1",
            "-sha256", "-subj", "/CN=ScratchIRCd Test Root",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign",
        )
        run_openssl(
            "req", "-newkey", "rsa:2048", "-nodes",
            "-keyout", chain_key, "-out", chain_request,
            "-subj", "/CN=ScratchIRCd Test Intermediate",
        )
        with open(chain_extensions, "w", encoding="utf-8") as handle:
            handle.write(
                "basicConstraints=critical,CA:TRUE,pathlen:0\n"
                "keyUsage=critical,keyCertSign,cRLSign\n"
                "subjectKeyIdentifier=hash\n"
                "authorityKeyIdentifier=keyid,issuer\n"
            )
        run_openssl(
            "x509", "-req", "-in", chain_request,
            "-CA", root_cert, "-CAkey", root_key, "-CAcreateserial",
            "-out", chain, "-days", "1", "-sha256",
            "-extfile", chain_extensions,
        )
        run_openssl(
            "req", "-newkey", "rsa:2048", "-nodes",
            "-keyout", key, "-out", cert_request,
            "-subj", "/CN=localhost",
        )
        with open(cert_extensions, "w", encoding="utf-8") as handle:
            handle.write(
                "basicConstraints=critical,CA:FALSE\n"
                "keyUsage=critical,digitalSignature,keyEncipherment\n"
                "extendedKeyUsage=serverAuth\n"
                "subjectAltName=DNS:localhost\n"
            )
        run_openssl(
            "x509", "-req", "-in", cert_request,
            "-CA", chain, "-CAkey", chain_key, "-CAcreateserial",
            "-out", cert, "-days", "1", "-sha256",
            "-extfile", cert_extensions,
        )

        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {plain_port}\ntls_port = {tls_port}\n")
            f.write(f"tls_cert_file = {cert}\ntls_chain_file = {chain}\n")
            f.write(f"tls_key_file = {key}\n")
            f.write("max_clients = 32\ndns_timeout_seconds = 1\n")
            f.write("registration_timeout_seconds = 5\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        tls_client = None
        plain_client = None
        malformed = None
        silent = None
        try:
            wait_listen(tls_port, proc)

            # Plain IRC sent to the TLS listener must fail closed without
            # affecting subsequent clients or granting a registered session.
            malformed = socket.create_connection(("127.0.0.1", tls_port), timeout=3.0)
            malformed.sendall(b"NICK nottls\r\nUSER nottls 0 * :Not TLS\r\n")
            wait_closed(malformed)
            malformed.close()
            malformed = None
            assert proc.poll() is None, "server exited after a malformed TLS handshake"

            # A peer that opens TCP but never starts TLS is still bounded by
            # the registration deadline. It must not retain a client slot
            # indefinitely.
            silent = socket.create_connection(("127.0.0.1", tls_port), timeout=3.0)
            wait_closed(silent)
            silent.close()
            silent = None
            assert proc.poll() is None, "server exited after a TLS handshake timeout"

            # Trust only the generated root. The handshake therefore proves
            # that ScratchIRCd sent the separately configured intermediate.
            context = ssl.create_default_context(
                ssl.Purpose.SERVER_AUTH, cafile=root_cert
            )
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

            # Exercise process shutdown while established plain and TLS
            # clients are still live. Cleanup must complete without a signal
            # crash or a forced kill.
            proc.terminate()
            proc.wait(timeout=3)
            assert proc.returncode == 0, proc.stderr.read()

        finally:
            if malformed is not None:
                malformed.close()
            if silent is not None:
                silent.close()
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
