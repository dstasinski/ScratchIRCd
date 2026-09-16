#!/usr/bin/env python3
"""NickServ verification-code brute-force throttle regression coverage."""

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
    raise RuntimeError("listener did not start")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick}")
    client.expect(f" 001 {nick} ")


def wait_verification_code(mailbox, duration=5.0):
    pattern = re.compile(r"Verification code: ([0-9]{3}-[0-9]{4})")
    deadline = time.monotonic() + duration

    while time.monotonic() < deadline:
        if os.path.exists(mailbox):
            with open(mailbox, "r", encoding="utf-8", errors="replace") as f:
                match = pattern.search(f.read())
            if match:
                return match.group(1)
        time.sleep(0.05)

    raise AssertionError("verification email was not received")


def wrong_codes(real_code, count):
    real_value = int(real_code.replace("-", ""))
    result = []

    candidate = (real_value + 1) % 10000000
    while len(result) < count:
        code = f"{candidate:07d}"
        if code != f"{real_value:07d}" and code not in result:
            result.append(code)
        candidate = (candidate + 1) % 10000000

    return result


def main():
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: test_nickserv_code_throttle.py "
            "scratchircd scratchircd-mkpasswd"
        )

    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(
        prefix="scratchircd-nickserv-code-throttle-"
    ) as td:
        port = free_port()
        admin_hash = subprocess.check_output(
            [mkpasswd, "adminpass"], text=True
        ).strip()

        mailbox = os.path.join(td, "mailbox.txt")
        sendmail = os.path.join(td, "fake-sendmail")

        with open(sendmail, "w", encoding="utf-8") as f:
            f.write("#!/bin/sh\n")
            f.write(f"cat >> {mailbox!r}\n")
            f.write(f"printf '\\n---END---\\n' >> {mailbox!r}\n")
        os.chmod(sendmail, 0o755)

        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\n")
            f.write("network_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\n")
            f.write("max_clients = 32\n")
            f.write("dns_timeout_seconds = 1\n")
            f.write("cloak_prefix = dru\n")
            f.write(
                "cloak_key = "
                "nickserv-code-throttle-test-key-0123456789abcdef\n"
            )
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\n")
            f.write("geoip_city_db = \n")
            f.write("geoip_asn_db = \n")
            f.write(f"sendmail_path = {sendmail}\n")
            f.write("mail_from = services@test.local\n")
            f.write("nickserv_verify_seconds = 60\n")
            f.write("nickserv_code_attempts_per_ip = 5\n")
            f.write("nickserv_code_attempts_per_account = 5\n")
            f.write("nickserv_code_attempt_window_seconds = 60\n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@*\n")

        proc = subprocess.Popen(
            [binary, conf],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        client = None
        try:
            wait_listen(port, proc)

            client = IRCClient(port)
            register(client, "Throttle")

            client.send("NICKSERV REGISTER firstpass")
            client.expect("Nickname registered and identified.")

            client.send("NICKSERV SET EMAIL throttle@example.test")
            client.expect("Verification email queued.")

            verification_code = wait_verification_code(mailbox)

            #
            # Five valid-format incorrect codes consume the entire dedicated
            # VERIFY budget. Pace attempts so the generic expensive-command
            # budget cannot become the limiter being exercised here.
            #
            for code in wrong_codes(verification_code, 5):
                client.send(f"NICKSERV VERIFY {code}")
                client.expect("Verification code is invalid or expired.")
                time.sleep(1.1)

            #
            # The genuine code is attempt six. It must be rejected even
            # though the underlying verification challenge is still valid.
            #
            client.send(f"NICKSERV VERIFY {verification_code}")
            client.expect("Verification code is invalid or expired.")

        finally:
            if client is not None:
                client.close()

            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=3)


if __name__ == "__main__":
    main()
