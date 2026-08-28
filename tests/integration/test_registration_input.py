#!/usr/bin/env python3
"""Registration and raw IRC input-boundary hardening coverage."""

import os
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

    def send_bytes(self, data):
        self.sock.sendall(data)

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

    def disconnected(self, duration=2.0):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(4096)
                if data == b"":
                    return True
            except socket.timeout:
                pass
            except (ConnectionResetError, BrokenPipeError, OSError):
                return True
        return False

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
            time.sleep(0.15)
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
            proc.kill(); proc.wait(timeout=3.0)


def new_client(port):
    c = IRCClient(port)
    c.expect("Looking up your hostname")
    return c


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_registration_input.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-input-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\nbans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\nchanserv_db = {td}/chanserv.db\n")
            f.write(f"memoserv_db = {td}/memoserv.db\nhistory_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            wait_listen(port, proc)

            # USER must reject malformed/overlong identity instead of silently
            # truncating it, then allow the client to retry correctly.
            c = new_client(port); clients.append(c)
            c.send("NICK ValidNick")
            c.send("USER bad@ident 0 * :Valid Real Name")
            c.expect(" 468 ValidNick :Invalid USER identity")
            c.send("USER " + ("u" * 32) + " 0 * :Valid Real Name")
            c.expect(" 468 ValidNick :Invalid USER identity")
            c.send("USER valid_ident 0 * :" + ("R" * 128))
            c.expect(" 468 ValidNick :Invalid USER identity")
            c.send("USER valid_ident 0 * :Valid Real Name")
            c.expect(" 001 ValidNick ", duration=5.0)

            # A single receive buffer may contain far more complete lines than
            # one event-loop dispatch budget. Leftover lines must be drained by
            # immediate loop turns rather than waiting for the normal 1s poll.
            packed = (b"\r\n" * 300) + b"PING :packed-tail\r\n"
            started = time.monotonic()
            c.send_bytes(packed)
            c.expect("PONG test.local ::packed-tail", duration=2.0)
            assert time.monotonic() - started < 1.5, "buffered line batches stalled between poll turns"

            # A maximum-size legal PING may need a shorter PONG because the
            # server adds its own prefix. The reply must still be emitted and
            # remain within the 510-octet IRC content envelope.
            c.send_bytes(b"PING :" + (b"x" * 504) + b"\r\n")
            pong_lines = c.expect("PONG test.local ::")
            pong = next(line for line in pong_lines if "PONG test.local ::" in line)
            assert len(pong.encode()) <= 510, (len(pong.encode()), pong)
            assert pong.endswith("x" * 480), pong

            # Existing NICK validation remains strict and retryable.
            n = new_client(port); clients.append(n)
            n.send("NICK " + ("N" * 32))
            n.expect(" 432 * ")
            n.send("NICK GoodNick")
            n.send("USER good 0 * :Good User")
            n.expect(" 001 GoodNick ", duration=5.0)

            # Client-supplied source prefixes are invalid on a client link.
            p = new_client(port); clients.append(p)
            p.send(":spoofed.example NICK Pretend")
            assert p.disconnected(), "client-supplied prefix was not disconnected"

            # Content beyond the 510-octet IRC limit is disconnected whether
            # delivered as a complete line or accumulated before LF arrives.
            long_complete = new_client(port); clients.append(long_complete)
            long_complete.send_bytes(b"PING :" + (b"x" * 505) + b"\r\n")
            assert long_complete.disconnected(), "overlong completed line was accepted"

            long_partial = new_client(port); clients.append(long_partial)
            long_partial.send_bytes(b"PING :" + (b"x" * 506))
            assert long_partial.disconnected(), "overlong partial line was retained"

            # NUL and embedded CR are framing attacks and must disconnect.
            nul = new_client(port); clients.append(nul)
            nul.send_bytes(b"NICK Safe\x00NICK Evil\r\n")
            assert nul.disconnected(), "embedded NUL was accepted"

            embedded_cr = new_client(port); clients.append(embedded_cr)
            embedded_cr.send_bytes(b"NICK Safe\rUSER evil 0 * :Injected\r\n")
            assert embedded_cr.disconnected(), "embedded CR was accepted"

            # Legitimate IRC control payloads such as CTCP SOH remain usable.
            ctcp = new_client(port); clients.append(ctcp)
            ctcp.send("NICK CtcpUser")
            ctcp.send("USER ctcp 0 * :CTCP User")
            ctcp.expect(" 001 CtcpUser ", duration=5.0)
            ctcp.send_bytes(b"NOTICE test.local :\x01VERSION Client 1.0\x01\r\n")
            time.sleep(0.2)
            assert not ctcp.disconnected(0.25), "legitimate CTCP SOH payload was rejected"
        finally:
            for c in clients:
                c.close()
            stop(proc)

    print("registration input hardening tests passed")


if __name__ == "__main__":
    main()
