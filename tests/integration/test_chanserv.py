#!/usr/bin/env python3
"""End-to-end ChanServ registration, access, protected-role, and persistence coverage."""

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


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3.0)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_chanserv.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-chanserv-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        chanserv_db = os.path.join(td, "chanserv.db")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\n")
            f.write(f"bans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\n")
            f.write(f"chanserv_db = {chanserv_db}\n")
            f.write(f"history_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        alice = bob = carol = None
        try:
            wait_listen(port, proc)
            alice = IRCClient(port)
            register(alice, "Alice")
            alice.send("NICKSERV REGISTER chanpass")
            alice.expect("Nickname registered and identified.")

            bob = IRCClient(port)
            register(bob, "Bob")
            bob.send("NICKSERV REGISTER bobpass")
            bob.expect("Nickname registered and identified.")

            carol = IRCClient(port)
            register(carol, "Carol")
            carol.send("NICKSERV REGISTER carolpass")
            carol.expect("Nickname registered and identified.")

            alice.send("JOIN #persist")
            alice.expect(" JOIN #persist")
            alice.send("CHANSERV REGISTER #persist :Persistent test channel")
            alice.expect("Channel registered successfully.")
            alice.send("CHANSERV ACCESS #persist ADD Bob OP")
            alice.expect("Access set: Bob OP")
            alice.send("CHANSERV ACCESS #persist ADD Carol PROTECTED")
            alice.expect("Access set: Carol PROTECTED")
            alice.send("CHANSERV ACCESS #persist LIST")
            access = alice.expect("Carol:5")
            assert any("Bob:3" in line for line in access), access
            alice.send("CHANSERV SET #persist MLOCK +nt")
            alice.expect("Persistent mode lock updated.")
            alice.send("CHANSERV SET #persist TOPIC :Persistent ChanServ topic")
            alice.expect("Persistent topic updated.")
            alice.send("MODE #persist")
            modes = alice.expect(" 324 Alice #persist ")
            assert any("r" in line and "n" in line and "t" in line
                       for line in modes if " 324 Alice #persist " in line), modes
            alice.send("QUIT :restart test")
            alice.close(); alice = None
            bob.close(); bob = None
            carol.close(); carol = None
        finally:
            if alice is not None: alice.close()
            if bob is not None: bob.close()
            if carol is not None: carol.close()
            stop(proc)

        assert os.path.exists(chanserv_db), "chanserv database was not created"

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        traveler = bob = guardian = observer = None
        try:
            wait_listen(port, proc)

            observer = IRCClient(port)
            register(observer, "Observer")
            isupport = observer.expect(" 005 Observer ")
            isupport_line = next(line for line in isupport if " 005 Observer " in line)
            assert "PCHANNELS=#persist" in isupport_line, isupport

            traveler = IRCClient(port)
            register(traveler, "Traveler")
            traveler.send("IDENTIFY Alice chanpass")
            traveler.expect("Password accepted - you are now identified.")
            traveler.send("JOIN #persist")
            join_lines = traveler.expect(" 366 Traveler #persist ")
            assert any("~Traveler" in line for line in join_lines if " 353 Traveler " in line), join_lines
            assert any("Persistent ChanServ topic" in line for line in join_lines if " 332 Traveler " in line), join_lines
            traveler.send("MODE #persist")
            modes = traveler.expect(" 324 Traveler #persist ")
            mode_line = next(line for line in modes if " 324 Traveler #persist " in line)
            assert all(letter in mode_line for letter in ("r", "n", "t")), modes

            bob = IRCClient(port)
            register(bob, "Helper")
            bob.send("IDENTIFY Bob bobpass")
            bob.expect("Password accepted - you are now identified.")
            bob.send("JOIN #persist")
            bob_lines = bob.expect(" 366 Helper #persist ")
            assert any("@Helper" in line for line in bob_lines if " 353 Helper " in line), bob_lines

            guardian = IRCClient(port)
            register(guardian, "Guardian")
            guardian.send("IDENTIFY Carol carolpass")
            guardian.expect("Password accepted - you are now identified.")
            guardian.send("JOIN #persist")
            guardian_lines = guardian.expect(" 366 Guardian #persist ")
            assert any("&Guardian" in line for line in guardian_lines if " 353 Guardian " in line), guardian_lines

            # An ordinary OP cannot strip +a, ban, or kick a protected member.
            bob.send("MODE #persist -a Guardian")
            bob.expect(" 482 Helper #persist ")
            bob.send("MODE #persist +b Guardian!*@*")
            bob.expect(" 482 Helper #persist ")
            bob.send("KICK #persist Guardian :not allowed")
            bob.expect(" 484 Helper #persist ")

            # OWNER may ban a protected member; the authorization survives reconnect.
            traveler.send("MODE #persist +b Guardian!*@*")
            traveler.expect(" MODE #persist +b Guardian!*@*")
            guardian.send("PART #persist :testing protected ban")
            guardian.expect(" PART #persist ")
            guardian.send("JOIN #persist")
            guardian.expect(" 474 Guardian #persist ")
            traveler.send("MODE #persist -b Guardian!*@*")
            traveler.expect(" MODE #persist -b Guardian!*@*")
            guardian.send("JOIN #persist")
            guardian_lines = guardian.expect(" 366 Guardian #persist ")
            assert any("&Guardian" in line for line in guardian_lines if " 353 Guardian " in line), guardian_lines

            # OWNER may grant +a manually; a PROTECTED member may kick another +a.
            traveler.send("MODE #persist +a Helper")
            traveler.expect(" MODE #persist +a Helper")
            guardian.send("KICK #persist Helper :protected hierarchy")
            guardian.expect(" KICK #persist Helper :protected hierarchy")

            traveler.send("CHANSERV ACCESS #persist DEL Bob")
            traveler.expect("Access entry removed.")
            traveler.send("CHANSERV ACCESS #persist DEL Carol")
            traveler.expect("Access entry removed.")
            traveler.send("CHANSERV DROP #persist")
            traveler.expect("Channel registration dropped.")
            traveler.send("MODE #persist")
            modes = traveler.expect(" 324 Traveler #persist ")
            mode_line = next(line for line in modes if " 324 Traveler #persist " in line)
            assert "r" not in mode_line.split(" 324 Traveler #persist ", 1)[1].split()[0], modes

            fresh = IRCClient(port)
            try:
                register(fresh, "Fresh")
                isupport = fresh.expect(" 005 Fresh ")
                isupport_line = next(line for line in isupport if " 005 Fresh " in line)
                assert "PCHANNELS=" in isupport_line and "#persist" not in isupport_line, isupport
            finally:
                fresh.close()
        finally:
            if traveler is not None: traveler.close()
            if bob is not None: bob.close()
            if guardian is not None: guardian.close()
            if observer is not None: observer.close()
            stop(proc)


if __name__ == "__main__":
    main()
