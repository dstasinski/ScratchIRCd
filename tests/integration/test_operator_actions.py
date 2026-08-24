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

        open(motd, "w", encoding="utf-8").write("test\n")
        open(rules, "w", encoding="utf-8").write("test\n")
        with open(config, "w", encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
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
            admin.send("OPER root adminpass")
            admin.expect(" 381 alice :You are now a Network Administrator")

            # Channel logging is persistent ChanServ state but only opers and
            # above may toggle it, even when a non-oper is the channel founder.
            receiver.send("NICKSERV REGISTER bobpass")
            receiver.expect("Nickname registered and identified.")
            receiver.send("JOIN #OpsLog")
            receiver.expect(" 366 bob #OpsLog ")
            receiver.send("CHANSERV REGISTER #OpsLog :operator logging test")
            receiver.expect("Channel registered successfully.")
            receiver.send("CHANSERV SET #OpsLog LOGGING ON")
            receiver.expect("Only IRC operators and network administrators may change channel logging.")
            admin.send("CHANSERV SET #OpsLog LOGGING ON")
            admin.expect("Channel logging enabled.")

            admin.send("JOIN #OpsLog")
            admin.expect(" 366 alice #OpsLog ")
            receiver.send("PRIVMSG #OpsLog :logged message")
            admin.expect("PRIVMSG #OpsLog :logged message")
            receiver.send("NOTICE #OpsLog :logged notice")
            admin.expect("NOTICE #OpsLog :logged notice")
            receiver.send("MODE #OpsLog +s")
            receiver.send("MODE #OpsLog")
            mode_lines = receiver.expect(" 324 bob #OpsLog ")
            assert any("s" in line.split(" 324 bob #OpsLog ", 1)[1].split()[0]
                       for line in mode_lines if " 324 bob #OpsLog " in line), mode_lines

            quitter = IRCClient(port); clients.append(quitter)
            register(quitter, "quitter")
            quitter.send("JOIN #OpsLog")
            quitter.expect(" 366 quitter #OpsLog ")
            quitter.send("QUIT :logging quit test")
            quitter.close(); clients.remove(quitter)

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
            assert "left irc: Quit:  logging quit test" in joined, joined
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
            receiver.expect(" 221 bob +w")
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

            # Nickname shorthand: KLINE resolves to *@real_host (or *@real_ip),
            # uses configured duration/reason, disconnects matching clients, and expires.
            nickkline = IRCClient(port); clients.append(nickkline)
            register(nickkline, "nickkline")
            admin.send("KLINE nickkline")
            admin.expect("nickname kline default")
            nickkline.expect(" 465 nickkline ")
            time.sleep(2.2)
            after_kline = IRCClient(port); clients.append(after_kline)
            register(after_kline, "afterkline")

            # Nickname shorthand: ZLINE resolves to the target's exact real_ip.
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
