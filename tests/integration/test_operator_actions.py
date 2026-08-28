#!/usr/bin/env python3
"""End-to-end tests for permission-controlled operator actions."""

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
        self.sock.settimeout(0.2)
        self.buffer = b""

    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())

    def expect(self, needle, duration=3.0):
        deadline = time.monotonic() + duration
        lines = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode(errors="replace")
                lines.append(line)
                if needle in line:
                    return lines
            try:
                data = self.sock.recv(4096)
                if not data:
                    break
                self.buffer += data
            except socket.timeout:
                pass
        raise AssertionError(f"expected {needle!r}; got {lines!r}")

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
            stderr = proc.stderr.read() if proc.stderr else ""
            raise RuntimeError(f"server exited early: {stderr!r}")
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=0.1)
            sock.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server did not listen")


def register(client, nick):
    client.send(f"NICK {nick}")
    client.send(f"USER {nick} 0 * :{nick} User")
    client.expect(f" 001 {nick} ")


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
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_operator_actions.py scratchircd scratchircd-mkpasswd")

    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.abspath(sys.argv[2])

    with tempfile.TemporaryDirectory(prefix="scratchircd-oper-actions-") as tmp:
        port = free_port()
        data_dir = os.path.join(tmp, "data")
        os.makedirs(data_dir, exist_ok=True)
        config = os.path.join(tmp, "ircd.conf")
        motd = os.path.join(tmp, "motd.txt")
        rules = os.path.join(tmp, "rules.txt")
        admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()
        server_name = "s" * 63

        open(motd, "w", encoding="utf-8").write("test\n")
        open(rules, "w", encoding="utf-8").write("test\n")
        with open(config, "w", encoding="utf-8") as f:
            f.write(f"server_name = {server_name}\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"motd_file = {motd}\nrules_file = {rules}\n")
            f.write("admin_email = admin@example.test\n")
            f.write(f"operators_db = {data_dir}/operators.db\n")
            f.write(f"bans_db = {data_dir}/bans.db\n")
            f.write(f"nickserv_db = {data_dir}/nickserv.db\n")
            f.write(f"chanserv_db = {data_dir}/chanserv.db\n")
            f.write(f"memoserv_db = {data_dir}/memoserv.db\n")
            f.write(f"history_db = {data_dir}/history.db\n")
            f.write("kline_default_duration_seconds = 2\n")
            f.write("kline_default_reason = nickname kline default\n")
            f.write("zline_default_duration_seconds = 2\n")
            f.write("zline_default_reason = nickname zline default\n")
            f.write("netadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\n")
            f.write("netadmin_hostmask = *!*@127.0.0.1\n")

        proc = subprocess.Popen([binary, config], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, cwd=tmp)
        clients = []
        try:
            wait_listen(port, proc)
            admin = IRCClient(port); clients.append(admin)
            receiver = IRCClient(port); clients.append(receiver)
            register(admin, "alice")
            register(receiver, "bob")

            receiver.send("NICKSERV REGISTER bobpass")
            receiver.expect("Nickname registered and identified.")
            admin.send("NICKSERV REGISTER alicepass")
            admin.expect("Nickname registered and identified.")
            admin.send("OPER root adminpass")
            admin.expect(" 381 alice :You are now a Network Administrator")

            # NSINFO must not silently disappear when every persisted identity
            # field is valid but the rendered NOTICE exceeds one IRC envelope.
            long_account = "n" * 31
            long_vhost = "v" * 63
            long_email = "e" * 64 + "@" + "d" * 185 + ".com"
            assert len(long_email) == 254
            long_client = IRCClient(port); clients.append(long_client)
            register(long_client, long_account)
            long_client.send("NICKSERV REGISTER longpass")
            long_client.expect("Nickname registered and identified.")
            admin.send(f"NSSET {long_account} VHOST {long_vhost}")
            admin.expect("NickServ account updated.")
            admin.send(f"NSSET {long_account} EMAIL {long_email}")
            admin.expect("NickServ account updated.")
            admin.send(f"NSINFO {long_account}")
            info_head = admin.expect(f"NICKSERV {long_account} enabled=1")
            info_tail = admin.expect(long_email[-32:])
            info_lines = info_head + info_tail
            prefix = f":{server_name} NOTICE alice :"
            payloads = [line[len(prefix):] for line in info_lines if line.startswith(prefix)]
            expected_info = (
                f"NICKSERV {long_account} enabled=1 vhost={long_vhost} "
                f"email={long_email} email_verified=1 created="
            )
            joined_payload = "".join(payloads)
            assert expected_info in joined_payload, payloads
            assert " updated=" in joined_payload, payloads
            assert all(len(line.encode()) <= 510 for line in info_lines), info_lines

            # Netadmin creates the registration, then assigns founder ownership
            # to the ordinary account. Founder status still does not imply oper
            # authority over channel logging.
            admin.send("JOIN #OpsLog")
            admin.expect(" 366 alice #OpsLog ")
            admin.send("CHANSERV REGISTER #OpsLog :operator logging test")
            admin.expect("Channel registered successfully.")
            admin.send("CSSET #OpsLog FOUNDER Bob")
            admin.expect("ChanServ channel updated.")
            receiver.send("JOIN #OpsLog")
            receiver.expect(" 366 bob #OpsLog ")
            receiver.send("CHANSERV SET #OpsLog LOGGING ON")
            receiver.expect("Only IRC operators and network administrators may change channel logging.")
            admin.send("CHANSERV SET #OpsLog LOGGING ON")
            admin.expect("Channel logging enabled.")

            receiver.send("PRIVMSG #OpsLog :logged message")
            admin.expect("PRIVMSG #OpsLog :logged message")
            receiver.send("NOTICE #OpsLog :logged notice")
            admin.expect("NOTICE #OpsLog :logged notice")

            receiver.send("MODE #OpsLog +s")
            receiver.expect(" 974 bob s :Mode is locked by ChanServ")
            receiver.send("MODE #OpsLog")
            mode_lines = receiver.expect(" 324 bob #OpsLog +r")
            assert any(" 324 bob #OpsLog +r" in line for line in mode_lines), mode_lines

            quitter = IRCClient(port); clients.append(quitter)
            register(quitter, "quitter")
            quitter.send("JOIN #OpsLog")
            quitter.expect(" 366 quitter #OpsLog ")
            quitter.send("QUIT :logging quit test")
            quitter.close(); clients.remove(quitter)
            admin.expect(" QUIT :logging quit test")

            dropper = IRCClient(port); clients.append(dropper)
            register(dropper, "dropper")
            dropper.send("JOIN #OpsLog")
            dropper.expect(" 366 dropper #OpsLog ")
            dropper.close(); clients.remove(dropper)
            admin.expect(" QUIT :Client quit")

            admin.send("PART #OpsLog :logging part test")
            admin.expect(" PART #OpsLog :logging part test")
            admin.send("CHANSERV SET #OpsLog LOGGING OFF")
            admin.expect("Channel logging disabled.")
            receiver.send("PRIVMSG #OpsLog :after-disabled")
            time.sleep(0.1)

            suffix = time.strftime("%d%b%Y", time.localtime())
            log_path = os.path.join(tmp, "logs", f"OpsLog.log.{suffix}")
            assert os.path.exists(log_path), log_path
            with open(log_path, "r", encoding="utf-8", errors="replace") as log_file:
                log_lines = [line.rstrip("\n") for line in log_file]
            assert log_lines, log_lines
            timestamp_re = re.compile(r"^\[[0-2][0-9]:[0-5][0-9]:[0-5][0-9]\]")
            assert all(timestamp_re.match(line) for line in log_lines), log_lines
            assert "---" in log_lines[0] and "---" in log_lines[-1], log_lines
            joined = "\n".join(log_lines)
            assert "alice (alice@" in joined and " joined #OpsLog." in joined, joined
            assert "<bob> logged message" in joined, joined
            assert "-bob- logged notice" in joined, joined
            assert "quitter (quitter@" in joined and " joined #OpsLog." in joined, joined
            assert joined.count("left irc: Quit:  logging quit test") == 1, joined
            assert "dropper (dropper@" in joined and "left irc: Quit:  Client quit" in joined, joined
            assert "alice (alice@" in joined and "left #OpsLog: logging part test" in joined, joined
            assert "MODE #OpsLog" not in joined, joined
            assert "after-disabled" not in joined, joined

            receiver.send("STATS")
            stats_help = receiver.expect(" 219 bob ? :End of /STATS report")
            assert any("STATS u - server uptime" in line for line in stats_help), stats_help
            assert any("STATS g - persistent GeoBAN policies" in line for line in stats_help), stats_help

            receiver.send("STATS k")
            receiver.expect(" 481 bob ")
            receiver.send("STATS z")
            receiver.expect(" 481 bob ")
            receiver.send("STATS g")
            receiver.expect(" 481 bob ")

            receiver.send("MODE bob +w")
            mode_lines = receiver.expect(" 221 bob ")
            assert any("w" in line.rsplit(" ", 1)[-1]
                       for line in mode_lines if " 221 bob " in line), mode_lines
            admin.send("WALLOPS :maintenance test")
            receiver.expect(" WALLOPS :maintenance test")

            victim = IRCClient(port); clients.append(victim)
            register(victim, "killme")
            admin.send("KILL killme :operator test")
            victim.expect(" KILL killme :operator test")

            admin.send("KLINE blocked@127.0.0.1 :kline test")
            admin.expect("NOTICE alice :KLINE added: blocked@127.0.0.1")
            admin.send("STATS k")
            stats_k = admin.expect(" 219 alice k :End of /STATS report")
            assert any(" 216 alice blocked@127.0.0.1 root :kline test" in line
                       for line in stats_k), stats_k
            blocked = IRCClient(port); clients.append(blocked)
            blocked.send("NICK blocked")
            blocked.send("USER blocked 0 * :Blocked User")
            blocked.expect(" 465 blocked ")
            admin.send("KLINE -blocked@127.0.0.1")
            admin.expect("NOTICE alice :KLINE removed: blocked@127.0.0.1")

            admin.send("ZLINE 127.0.0.1 :zline test")
            admin.expect("NOTICE alice :ZLINE added: 127.0.0.1")
            admin.send("STATS z")
            stats_z = admin.expect(" 219 alice z :End of /STATS report")
            assert any(" 210 alice :ZLINE 127.0.0.1 set-by=root" in line and
                       "reason=zline test" in line for line in stats_z), stats_z
            zed = IRCClient(port); clients.append(zed)
            zed.send("NICK zed")
            zed.send("USER zed 0 * :Zed User")
            zed.expect(" 465 zed ")
            admin.send("ZLINE -127.0.0.1")
            admin.expect("NOTICE alice :ZLINE removed: 127.0.0.1")

            nickkline = IRCClient(port); clients.append(nickkline)
            register(nickkline, "nickkline")
            admin.send("KLINE nickkline")
            admin.expect("nickname kline default")
            nickkline.expect(" 465 nickkline ")
            time.sleep(2.2)
            after_kline = IRCClient(port); clients.append(after_kline)
            register(after_kline, "afterkline")

            nickzline = IRCClient(port); clients.append(nickzline)
            register(nickzline, "nickzline")
            admin.send("ZLINE nickzline")
            admin.expect("nickname zline default")
            nickzline.expect(" 465 nickzline ")
            time.sleep(2.2)
            after_zline = IRCClient(port); clients.append(after_zline)
            register(after_zline, "afterzline")

            admin.send("REHASH")
            admin.expect(" 382 alice ")
        finally:
            for client in clients:
                client.close()
            stop_server(proc)

    print("operator action integration tests passed")


if __name__ == "__main__":
    main()
