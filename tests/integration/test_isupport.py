#!/usr/bin/env python3
"""End-to-end validation of ScratchIRCd RPL_ISUPPORT advertisement."""

import os
import socket
import sqlite3
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
            if f" 005 {nick} ".encode() in data and b" :are supported by this server\r\n" in data:
                time.sleep(0.05)
        except socket.timeout:
            break
    return [line.rstrip(b"\r").decode(errors="replace")
            for line in data.split(b"\n") if line]


def register_and_read(port, nick):
    sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
    sock.sendall(f"NICK {nick}\r\nUSER {nick} 0 * :ISUPPORT Test\r\n".encode())
    return sock, read_registration(sock, nick)


def stop_server(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)


def irc_fold(value):
    table = str.maketrans("{}|~", "[]\\^")
    return value.lower().translate(table)


def irc_nocase(left, right):
    a, b = irc_fold(left), irc_fold(right)
    return (a > b) - (a < b)


def open_chanserv_db(path):
    db = sqlite3.connect(path)
    db.create_collation("IRCNOCASE", irc_nocase)
    return db


def assert_base_isupport(lines, nick):
    replies = [line for line in lines if f" 005 {nick} " in line]
    assert len(replies) >= 2, replies
    for line in replies:
        assert len((line + "\r\n").encode()) <= 512, line
        payload = line.split(f" 005 {nick} ", 1)[1]
        payload = payload.rsplit(" :are supported by this server", 1)[0]
        assert len(payload.split()) <= 13, line
    joined = " ".join(replies)
    expected = [
        "CASEMAPPING=rfc1459", "CHANTYPES=#&", "PREFIX=(qaohv)~&@%+",
        "CHANMODES=beI,,kljBL,AciKMmnOprRSstTVz", "CHANLIMIT=#&:32",
        "NICKLEN=31", "USERLEN=31", "HOSTLEN=255", "CHANNELLEN=63",
        "TOPICLEN=390", "KICKLEN=255", "MODES=32", "NETWORK=RuntimeNet",
        "EXCEPTS=e", "INVEX=I", "WATCH=128", "SILENCE=64",
        "TARGMAX=PRIVMSG:1,NOTICE:1,JOIN:1,PART:1,KICK:1,NAMES:1",
        "MSGREFTYPES=timestamp", "CHATHISTORY=100", "PCHANNELS=",
    ]
    for token in expected:
        assert token in joined, (token, replies)
    assert "STATUSMSG=" not in joined, replies
    assert "MAXLIST=" not in joined, replies
    return replies


def pchannels_from_replies(replies, nick):
    names = []
    pchannel_lines = 0
    for line in replies:
        payload = line.split(f" 005 {nick} ", 1)[1]
        payload = payload.rsplit(" :are supported by this server", 1)[0]
        for part in payload.split():
            if part.startswith("PCHANNELS="):
                pchannel_lines += 1
                names.extend(name for name in part.split("=", 1)[1].split(",") if name)
    return names, pchannel_lines


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
        chanserv_db = os.path.join(data_dir, "chanserv.db")
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
            f.write(f"chanserv_db = {chanserv_db}\n")
            f.write(f"memoserv_db = {data_dir}/memoserv.db\n")
            f.write(f"history_db = {data_dir}/history.db\n")

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        sock = None
        try:
            wait_listen(port, proc)
            sock, lines = register_and_read(port, "Support")
            replies = assert_base_isupport(lines, "Support")
            names, pchannel_lines = pchannels_from_replies(replies, "Support")
            assert names == [], names
            assert pchannel_lines == 1, replies
        finally:
            if sock is not None:
                try: sock.close()
                except OSError: pass
            stop_server(proc)

        db = open_chanserv_db(chanserv_db)
        try:
            for i in range(80):
                db.execute(
                    "INSERT INTO channels(name,founder,description,enabled) VALUES(?,?,?,1)",
                    (f"#PersistentChannel{i:03d}", "founder", "test"))
            db.commit()
        finally:
            db.close()

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        sockets = []
        moved_db = chanserv_db + ".moved"
        try:
            wait_listen(port, proc)
            first, lines = register_and_read(port, "Bounded")
            sockets.append(first)
            replies = assert_base_isupport(lines, "Bounded")
            names, pchannel_lines = pchannels_from_replies(replies, "Bounded")
            expected_names = [f"#PersistentChannel{i:03d}" for i in range(80)]
            assert names == expected_names, names
            assert pchannel_lines > 1, replies

            db = open_chanserv_db(chanserv_db)
            try:
                indexes = {row[1] for row in db.execute("PRAGMA index_list(channels)")}
            finally:
                db.close()
            assert "channels_enabled_name_idx" in indexes, indexes

            # After the first registration populates the process-local cache,
            # remove the backing DB pathname. A second registration must still
            # receive the complete PCHANNELS snapshot without reopening SQLite.
            os.rename(chanserv_db, moved_db)
            cached, lines = register_and_read(port, "Cached")
            sockets.append(cached)
            replies = assert_base_isupport(lines, "Cached")
            cached_names, cached_lines = pchannels_from_replies(replies, "Cached")
            assert cached_names == expected_names, cached_names
            assert cached_lines == pchannel_lines, (cached_lines, pchannel_lines)
            os.rename(moved_db, chanserv_db)
        finally:
            if os.path.exists(moved_db) and not os.path.exists(chanserv_db):
                os.rename(moved_db, chanserv_db)
            for sock in sockets:
                try: sock.close()
                except OSError: pass
            stop_server(proc)


if __name__ == "__main__":
    main()
