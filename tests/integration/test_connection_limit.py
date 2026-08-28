#!/usr/bin/env python3
"""Per-IP concurrent connection limit and exemption integration coverage."""

import os
import socket
import subprocess
import sys
import tempfile
import time


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
            # Give the event loop one turn to remove this readiness probe so it
            # cannot transiently consume a per-IP slot in the real test.
            time.sleep(0.25)
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
            proc.kill()
            proc.wait(timeout=3.0)


def connect(port):
    sock = socket.create_connection(("127.0.0.1", port), timeout=2.0)
    sock.settimeout(1.0)
    return sock


def accepted(sock):
    """An admitted plaintext client receives the asynchronous DNS notice."""
    try:
        data = sock.recv(4096)
        return bool(data)
    except (ConnectionResetError, BrokenPipeError, OSError):
        return False


def rejected(sock):
    try:
        sock.sendall(b"PING :limit-check\r\n")
        data = sock.recv(4096)
        return data == b""
    except (ConnectionResetError, BrokenPipeError, OSError):
        return True


def write_config(path, port, extra=""):
    td = os.path.dirname(path)
    with open(path, "w", encoding="utf-8") as f:
        f.write("server_name = test.local\nnetwork_name = TestNet\n")
        f.write("bind_address = 127.0.0.1\n")
        f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
        f.write(f"operators_db = {td}/operators.db\n")
        f.write(f"bans_db = {td}/bans.db\n")
        f.write(f"nickserv_db = {td}/nickserv.db\n")
        f.write(f"chanserv_db = {td}/chanserv.db\n")
        f.write(f"memoserv_db = {td}/memoserv.db\n")
        f.write(f"history_db = {td}/history.db\n")
        f.write("geoip_city_db = \ngeoip_asn_db = \n")
        f.write(extra)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_connection_limit.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-connlimit-") as td:
        conf = os.path.join(td, "ircd.conf")

        # Ordinary transport IPs are limited immediately, before registration.
        port = free_port()
        write_config(conf, port, "max_connections_per_ip = 2\n")
        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        sockets = []
        try:
            wait_listen(port, proc)
            first = connect(port); sockets.append(first); assert accepted(first)
            second = connect(port); sockets.append(second); assert accepted(second)
            third = connect(port); sockets.append(third); assert rejected(third), "third same-IP connection was admitted"

            first.close(); sockets.remove(first)
            time.sleep(0.35)
            replacement = connect(port); sockets.append(replacement)
            assert accepted(replacement), "capacity was not released after disconnect"
        finally:
            for sock in sockets:
                try: sock.close()
                except OSError: pass
            stop(proc)

        # An explicitly exempt literal IP bypasses the per-IP cap.
        port = free_port()
        write_config(conf, port,
                     "max_connections_per_ip = 1\n"
                     "connection_limit_exempt_ip = 127.0.0.1\n")
        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        sockets = []
        try:
            wait_listen(port, proc)
            for _ in range(3):
                sock = connect(port); sockets.append(sock)
                assert accepted(sock), "explicitly exempt IP was limited"
        finally:
            for sock in sockets:
                try: sock.close()
                except OSError: pass
            stop(proc)

        # A trusted WebIRC gateway source is automatically exempt, but its
        # supplied end-user real_ip is still subject to the configured cap.
        port = free_port()
        write_config(conf, port,
                     "max_connections_per_ip = 1\n"
                     "webirc_gateway = 127.0.0.1 gateway-secret\n")
        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        sockets = []
        try:
            wait_listen(port, proc)
            web1 = connect(port); sockets.append(web1); assert accepted(web1)
            web1.sendall(b"WEBIRC gateway-secret web.example user.example 203.0.113.25\r\n")
            web1.sendall(b"PING :web-one\r\n")
            data = web1.recv(4096)
            assert data, "first WebIRC end-user was disconnected"

            # The second transport connection is admitted because 127.0.0.1
            # is a configured trusted gateway, but WEBIRC must reject the same
            # supplied end-user IP.
            web2 = connect(port); sockets.append(web2); assert accepted(web2)
            web2.sendall(b"WEBIRC gateway-secret web.example user.example 203.0.113.25\r\n")
            deadline = time.monotonic() + 2.0
            response = b""
            closed = False
            while time.monotonic() < deadline:
                try:
                    chunk = web2.recv(4096)
                    if not chunk:
                        closed = True
                        break
                    response += chunk
                    if b"Too many concurrent connections" in response:
                        break
                except socket.timeout:
                    pass
            assert closed or b"Too many concurrent connections" in response, response

            # A different end-user identity is allowed through the same gateway.
            web3 = connect(port); sockets.append(web3); assert accepted(web3)
            web3.sendall(b"WEBIRC gateway-secret web.example other.example 203.0.113.26\r\n")
            web3.sendall(b"PING :web-three\r\n")
            data = web3.recv(4096)
            assert data, "different WebIRC end-user IP was rejected"

            # Equivalent IPv6 spellings are one numeric identity. The first
            # spelling consumes the only slot; an expanded spelling of the same
            # address must be rejected rather than treated as a different IP.
            web4 = connect(port); sockets.append(web4); assert accepted(web4)
            web4.sendall(b"WEBIRC gateway-secret web.example v6-one.example 2001:db8::1\r\n")
            web4.sendall(b"PING :web-v6-one\r\n")
            data = web4.recv(4096)
            assert data, "first IPv6 WebIRC end-user was disconnected"

            web5 = connect(port); sockets.append(web5); assert accepted(web5)
            web5.sendall(b"WEBIRC gateway-secret web.example v6-two.example 2001:0db8:0:0:0:0:0:1\r\n")
            deadline = time.monotonic() + 2.0
            response = b""
            closed = False
            while time.monotonic() < deadline:
                try:
                    chunk = web5.recv(4096)
                    if not chunk:
                        closed = True
                        break
                    response += chunk
                    if b"Too many concurrent connections" in response:
                        break
                except socket.timeout:
                    pass
            assert closed or b"Too many concurrent connections" in response, response

            # WEBIRC metadata is persisted in fixed-size identity fields. Reject
            # values that cannot be represented exactly instead of silently
            # truncating them and accepting a different gateway identity.
            web6 = connect(port); sockets.append(web6); assert accepted(web6)
            overlong_gateway = b"g" * 128
            web6.sendall(b"WEBIRC gateway-secret " + overlong_gateway +
                         b" host.example 198.51.100.60\r\n")
            deadline = time.monotonic() + 2.0
            response = b""
            closed = False
            while time.monotonic() < deadline:
                try:
                    chunk = web6.recv(4096)
                    if not chunk:
                        closed = True
                        break
                    response += chunk
                    if b"Invalid WEBIRC parameters" in response:
                        break
                except socket.timeout:
                    pass
            assert closed or b"Invalid WEBIRC parameters" in response, response

            # The count follows the WEBIRC end-user identity and must be released
            # when that client disconnects. Reusing the same end-user address
            # after web1 closes catches stale entries in the IP-count index.
            web1.close(); sockets.remove(web1)
            time.sleep(0.35)
            web7 = connect(port); sockets.append(web7); assert accepted(web7)
            web7.sendall(b"WEBIRC gateway-secret web.example reused.example 203.0.113.25\r\n")
            web7.sendall(b"PING :web-reused\r\n")
            data = web7.recv(4096)
            assert data, "WEBIRC end-user capacity was not released after disconnect"
        finally:
            for sock in sockets:
                try: sock.close()
                except OSError: pass
            stop(proc)

    print("connection limit integration tests passed")


if __name__ == "__main__":
    main()
