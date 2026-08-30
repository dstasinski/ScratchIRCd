#!/usr/bin/env python3
"""Real-socket SendQ pressure and healthy-peer isolation coverage."""

import os
import socket
import subprocess
import sys
import tempfile
import time


class IRCClient:
    def __init__(self, port, receive_buffer=None):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if receive_buffer is not None:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer)
        self.sock.settimeout(3.0)
        self.sock.connect(("127.0.0.1", port))
        self.sock.settimeout(0.2)
        self.buffer = b""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def send_lines(self, lines):
        payload = "".join(line + "\r\n" for line in lines).encode()
        self.sock.settimeout(5.0)
        try:
            self.sock.sendall(payload)
        finally:
            self.sock.settimeout(0.2)

    def expect(self, needle, duration=5.0):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
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
        raise AssertionError(f"expected {needle!r}; got {seen!r}")

    def drain(self, duration=0.3):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            try:
                data = self.sock.recv(8192)
                if not data:
                    return
                self.buffer += data
            except socket.timeout:
                pass
        self.buffer = b""

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
            probe = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            probe.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("listener did not start")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick.lower()} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")
    client.drain()


def is_online(client, nick):
    client.send(f"ISON {nick}")
    response = client.expect(" 303 ")
    return nick in response.rsplit(":", 1)[-1].split()


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3.0)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_sendq_pressure.py scratchircd scratchircd-mkpasswd")
    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-sendq-") as directory:
        port = free_port()
        config = os.path.join(directory, "ircd.conf")
        admin_hash = subprocess.check_output(
            [mkpasswd, "adminpass"], text=True
        ).strip()
        with open(config, "w", encoding="utf-8") as handle:
            handle.write("server_name = test.local\nnetwork_name = TestNet\n")
            handle.write("bind_address = 127.0.0.1\n")
            handle.write(f"port = {port}\nmax_clients = 16\ndns_timeout_seconds = 1\n")
            handle.write("output_queue_max_bytes = 4096\n")
            handle.write("netadmin_name = root\n")
            handle.write(f"netadmin_password_hash = {admin_hash}\n")
            handle.write("netadmin_hostmask = *!*@127.0.0.1\n")
            for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
                handle.write(f"{name}_db = {directory}/{name}.db\n")
            handle.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen(
            [binary, config], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        clients = []
        try:
            wait_listen(port, proc)

            # Set a deliberately small TCP receive buffer before connect so a
            # client that stops reading quickly forces server-side queuing.
            slow = IRCClient(port, receive_buffer=1024)
            clients.append(slow)
            assert slow.sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF) <= 8192
            register(slow, "Slow")
            slow.send("JOIN #pressure")
            slow.expect(" 366 Slow #pressure ")
            slow.drain()

            observer = IRCClient(port)
            clients.append(observer)
            register(observer, "Observer")
            assert is_online(observer, "Slow")

            sender = IRCClient(port)
            clients.append(sender)
            register(sender, "Pressure")
            sender.send("OPER root adminpass")
            sender.expect(" 381 Pressure ")
            sender.send("JOIN #pressure")
            sender.expect(" 366 Pressure #pressure ")
            sender.drain()

            payload = "X" * 420
            slow_disconnected = False
            for batch_index in range(10):
                # The authenticated test netadmin is exempt from inbound flood
                # throttling, allowing enough legal IRC traffic to fill the
                # bounded Linux TCP send buffer before exercising the 4 KiB
                # application SendQ. Each batch is finite and under 1.2 MiB.
                sender.send_lines(
                    f"PRIVMSG #pressure :{batch_index:02d}-{index:04d}-{payload}"
                    for index in range(2500)
                )

                # A separate client must remain responsive while another
                # client's socket and bounded output queue are saturated.
                observer.send(f"PING :pressure-{batch_index}")
                observer.expect(f"PONG test.local ::pressure-{batch_index}")
                time.sleep(0.1)
                if not is_online(observer, "Slow"):
                    slow_disconnected = True
                    break

            assert slow_disconnected, "slow reader did not exceed its bounded SendQ"
            assert proc.poll() is None, "server exited under SendQ pressure"

            # The preceding bounded flood can leave legitimate commands in the
            # client-to-server kernel buffer even after the slow reader has
            # been dropped. Use the same finite five-second write allowance as
            # the pressure batches instead of the normal 200 ms probe timeout.
            sender.send_lines(["PING :sender-alive"])
            sender.expect("PONG test.local ::sender-alive")

            # The disconnected client's slot and nickname index must both be
            # reclaimed rather than leaking capacity or stale identity state.
            replacement = IRCClient(port)
            clients.append(replacement)
            register(replacement, "Slow")
            replacement.send("PING :replacement")
            replacement.expect("PONG test.local ::replacement")
        finally:
            for client in clients:
                client.close()
            stop(proc)

    print("SendQ pressure integration test passed")


if __name__ == "__main__":
    main()
