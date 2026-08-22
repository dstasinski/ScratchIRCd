#!/usr/bin/env python3
"""End-to-end validation of ScratchIRCd RPL_ISUPPORT advertisement."""

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
            stderr = proc.stderr.read() if proc.stderr else ""
            raise RuntimeError(f"server exited early: {stderr!r}")
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            sock.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not listen")


def read_registration(sock, nick):
    sock.settimeout(0.2)
    data = b""
    deadline = time.monotonic() + 4.0
    while time.monotonic() < deadline:
        try:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
            if f" 005 {nick} ".encode() in data and data.count(f" 005 {nick} ".encode()) >= 2:
                break
        except socket.timeout:
            pass
    return [line.rstrip(b"\r").decode(errors="replace")
            for line in data.split(b"\n") if line]


def stop_server(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_isupport.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-isupport-") as tmp:
        port = free_port()
        conf = os.path.join(tmp, "ircd.conf")
        motd = os.path.join(tmp, "motd.txt")
        rules = os.path.join(tmp, "rules.txt")
        data_dir = os.path.join(tmp, "data")
        os.mkdir(data_dir)
        open(motd, "w", encoding="utf-8").write("test\n")
        open(rules, "w", encoding="utf-8").write("test\n")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = RuntimeNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"motd_file = {motd}\nrules_file = {rules}\n")
            f.write(f"operators_db = {data_dir}/operators.db\n")
            f.write(f"bans_db = {data_dir}/bans.db\n")
            f.write(f"nickserv_db = {data_dir}/nickserv.db\n")
            f.write(f"chanserv_db = {data_dir}/chanserv.db\n")
            f.write(f"memoserv_db = {data_dir}/memoserv.db\n")
            f.write(f"history_db = {data_dir}/history.db\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        sock = None
        try:
            wait_listen(port, proc)
            sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
            sock.sendall(b"NICK Support\r\nUSER Support 0 * :ISUPPORT Test\r\n")
            lines = read_registration(sock, "Support")
            replies = [line for line in lines if " 005 Support " in line]
            assert len(replies) == 2, replies

            # RFC limit: at most thirteen advertised tokens per 005 reply.
            for line in replies:
                payload = line.split(" 005 Support ", 1)[1]
                payload = payload.rsplit(" :are supported by this server", 1)[0]
                assert len(payload.split()) <= 13, line

            joined = " ".join(replies)
            expected = [
                "CASEMAPPING=rfc1459",
                "CHANTYPES=#&",
                "PREFIX=(qaohv)~&@%+",
                "CHANMODES=beI,,kljBL,AciKMmnOprRSstTVz",
                "CHANLIMIT=#&:32",
                "NICKLEN=31",
                "USERLEN=31",
                "HOSTLEN=255",
                "CHANNELLEN=63",
                "TOPICLEN=390",
                "KICKLEN=255",
                "MODES=32",
                "NETWORK=RuntimeNet",
                "EXCEPTS=e",
                "INVEX=I",
                "WATCH=128",
                "SILENCE=64",
                "TARGMAX=PRIVMSG:1,NOTICE:1,JOIN:1,PART:1,KICK:1,NAMES:1",
                "MSGREFTYPES=timestamp",
                "CHATHISTORY=100",
                "PCHANNELS=",
            ]
            for token in expected:
                assert token in joined, (token, replies)

            assert "STATUSMSG=" not in joined, replies
            assert "MAXLIST=" not in joined, replies
        finally:
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
            stop_server(proc)


if __name__ == "__main__":
    main()
