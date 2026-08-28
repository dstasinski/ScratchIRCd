#!/usr/bin/env python3
"""Connection and channel lifecycle churn over real TCP sockets."""

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

    def send_bytes(self, data):
        self.sock.sendall(data)

    def read_lines(self, duration=0.25):
        deadline = time.monotonic() + duration
        lines = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                lines.append(raw.rstrip(b"\r").decode(errors="replace"))
            try:
                data = self.sock.recv(8192)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            lines.append(raw.rstrip(b"\r").decode(errors="replace"))
        return lines

    def expect(self, needle, duration=5.0):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
            for line in self.read_lines(0.1):
                seen.append(line)
                if needle in line:
                    return line
        raise AssertionError(f"expected {needle!r}; got {seen!r}")

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
    raise RuntimeError("server did not begin listening")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3.0)


def names_until_end(client, nick, channel):
    client.send(f"NAMES {channel}")
    deadline = time.monotonic() + 5.0
    lines = []
    while time.monotonic() < deadline:
        lines.extend(client.read_lines(0.1))
        if any(f" 366 {nick} {channel} " in line for line in lines):
            return lines
    raise AssertionError(f"NAMES did not finish for {channel}: {lines!r}")


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_connection_lifecycle.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-lifecycle-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        with open(conf, "w", encoding="utf-8") as handle:
            handle.write("server_name = test.local\nnetwork_name = TestNet\n")
            handle.write("bind_address = 127.0.0.1\n")
            handle.write(f"port = {port}\nmax_clients = 64\ndns_timeout_seconds = 1\n")
            handle.write("registration_timeout_seconds = 10\n")
            for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
                handle.write(f"{name}_db = {td}/{name}.db\n")
            handle.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen(
            [binary, conf], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        survivor = None
        live = []
        try:
            wait_listen(port, proc)
            survivor = IRCClient(port)
            register(survivor, "Survivor")
            survivor.send("JOIN #stable")
            survivor.expect(" 366 Survivor #stable ")

            # Drop sockets at several pre-registration points. These clients must
            # release capacity without leaving nick/hash or partial-input state.
            for index in range(40):
                raw = socket.create_connection(("127.0.0.1", port), timeout=2.0)
                if index % 3 == 0:
                    raw.sendall(f"NICK Pending{index}\r\n".encode())
                elif index % 3 == 1:
                    raw.sendall(b"NICK Partial")
                raw.close()

            # Repeatedly mix orderly QUIT with abrupt registered disconnects while
            # every transient client shares two channels with the survivor.
            for round_index in range(8):
                batch = []
                for member_index in range(6):
                    nick = f"R{round_index}M{member_index}"
                    client = IRCClient(port)
                    live.append(client)
                    register(client, nick)
                    client.send("JOIN #stable")
                    client.expect(f" 366 {nick} #stable ")
                    client.send("JOIN #ephemeral")
                    client.expect(f" 366 {nick} #ephemeral ")
                    batch.append((client, nick))

                for client, _ in batch[:3]:
                    client.send("QUIT :orderly lifecycle test")
                    client.close()
                    live.remove(client)
                for client, _ in batch[3:]:
                    client.close()
                    live.remove(client)

                # Give the event loop a chance to consume EOF/HUP before the next
                # generation attempts to reuse capacity and channel state.
                time.sleep(0.08)
                assert proc.poll() is None, "server exited during connection churn"
                survivor.read_lines(0.05)

            # Every transient membership must be gone and the survivor must still
            # be fully usable.
            stable = names_until_end(survivor, "Survivor", "#stable")
            name_payload = " ".join(
                line.split(" #stable :", 1)[1]
                for line in stable
                if " 353 Survivor " in line and " #stable :" in line
            )
            assert "Survivor" in name_payload, stable
            assert not any(token.startswith("R") and "M" in token for token in name_payload.split()), name_payload

            survivor.send("PING :after-churn")
            survivor.expect("PONG test.local ::after-churn")

            # #ephemeral should have been destroyed when its last member left.
            # Recreating it must grant initial channel-operator authority.
            survivor.send("JOIN #ephemeral")
            survivor.expect(" 366 Survivor #ephemeral ")
            recreated = names_until_end(survivor, "Survivor", "#ephemeral")
            payload = " ".join(
                line.split(" #ephemeral :", 1)[1]
                for line in recreated
                if " 353 Survivor " in line and " #ephemeral :" in line
            )
            assert "~Survivor" in payload, recreated
        finally:
            for client in live:
                client.close()
            if survivor is not None:
                survivor.close()
            stop(proc)

    print("connection lifecycle churn tests passed")


if __name__ == "__main__":
    main()
