#!/usr/bin/env python3
"""ChanServ founder-transfer, enable/disable, and restart lifecycle coverage."""

import os
import subprocess
import sys
import tempfile

from test_chanserv import IRCClient, free_port, register, stop, wait_listen


def write_config(path, td, port, admin_hash):
    with open(path, "w", encoding="utf-8") as f:
        f.write("server_name = test.local\nnetwork_name = TestNet\n")
        f.write("bind_address = 127.0.0.1\n")
        f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
        f.write(f"operators_db = {td}/operators.db\n")
        f.write(f"bans_db = {td}/bans.db\n")
        f.write(f"nickserv_db = {td}/nickserv.db\n")
        f.write(f"chanserv_db = {td}/chanserv.db\n")
        f.write(f"history_db = {td}/history.db\n")
        f.write("geoip_city_db = \ngeoip_asn_db = \n")
        f.write("netadmin_name = root\n")
        f.write(f"netadmin_password_hash = {admin_hash}\n")
        f.write("netadmin_hostmask = *!*@127.0.0.1\n")


def member_token(lines, requester, nick):
    name_line = next(line for line in lines if f" 353 {requester} " in line)
    return next(token for token in name_line.rsplit(" :", 1)[1].split()
                if token.lstrip("~&@%+") == nick)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_chanserv_founder_transfer.py scratchircd")

    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.join(os.path.dirname(binary), "scratchircd-mkpasswd")
    admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()

    with tempfile.TemporaryDirectory(prefix="scratchircd-chanserv-transfer-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        write_config(conf, td, port, admin_hash)

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        alice = bob = None
        try:
            wait_listen(port, proc)

            alice = IRCClient(port)
            register(alice, "Alice")
            alice.send("NICKSERV REGISTER alicepass")
            alice.expect("Nickname registered and identified.")
            alice.send("OPER root adminpass")
            alice.expect(" 381 Alice :You are now a Network Administrator")

            bob = IRCClient(port)
            register(bob, "Bob")
            bob.send("NICKSERV REGISTER bobpass")
            bob.expect("Nickname registered and identified.")

            alice.send("JOIN #transfer")
            alice.expect(" 366 Alice #transfer ")
            alice.send("CHANSERV REGISTER #transfer :Transfer lifecycle")
            alice.expect("Channel registered successfully.")
            bob.send("JOIN #transfer")
            bob.expect(" 366 Bob #transfer ")

            # Disabling a registration makes ordinary ChanServ INFO treat the
            # channel as unregistered, then enabling restores the live policy.
            alice.send("CSSET #transfer ENABLED 0")
            alice.expect("ChanServ channel updated.")
            alice.send("CHANSERV INFO #transfer")
            alice.expect("Channel is not registered.")
            alice.send("CSSET #transfer ENABLED 1")
            alice.expect("ChanServ channel updated.")
            alice.send("CHANSERV INFO #transfer")
            alice.expect("founder=Alice")

            # Seed policy that must remain intact across founder transfer.
            alice.send("CHANSERV SET #transfer MLOCK +nt")
            alice.expect("Persistent mode lock updated.")
            alice.send("CHANSERV SET #transfer GREETING :Welcome after transfer")
            alice.expect("Greeting updated.")
            alice.send("CHANSERV SET #transfer SUCCESSOR Bob")
            alice.expect("Successor updated.")
            alice.send("CHANSERV INFO #transfer")
            alice.expect("successor=Bob")

            # Transfer founder through the network-administrator command. The
            # live cache must refresh immediately: Bob gains founder authority,
            # Alice loses ordinary founder-command authority despite still
            # being a network administrator, and the successor field is cleared
            # because the successor has become the founder.
            alice.send("CSSET #transfer FOUNDER Bob")
            alice.expect("ChanServ channel updated.")
            alice.send("CSINFO #transfer")
            csinfo = alice.expect("founder=Bob")
            assert any("topiclock=ON" in line and "successor=NONE" in line and
                       "greeting=Welcome after transfer" in line for line in csinfo), csinfo

            alice.send("CHANSERV SET #transfer GREETING :Alice should not be founder")
            alice.expect("Only the channel founder may change this setting.")
            bob.send("CHANSERV SET #transfer GREETING :Bob is founder now")
            bob.expect("Greeting updated.")
            bob.send("NAMES #transfer")
            names = bob.expect(" 366 Bob #transfer ")
            assert member_token(names, "Bob", "Bob") == "~Bob", names

            alice.close(); alice = None
            bob.close(); bob = None
        finally:
            if alice is not None: alice.close()
            if bob is not None: bob.close()
            stop(proc)

        # Restart verifies that founder transfer and policy fields were stored
        # in SQLite and restored into the first recreated live channel.
        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        founder = old_founder = None
        try:
            wait_listen(port, proc)

            founder = IRCClient(port)
            register(founder, "Founder")
            founder.send("IDENTIFY Bob bobpass")
            founder.expect("Password accepted - you are now identified.")
            founder.send("JOIN #transfer")
            join_lines = founder.expect(" 366 Founder #transfer ")
            assert any("NOTICE Founder :Bob is founder now" in line for line in join_lines), join_lines
            founder.send("MODE #transfer")
            modes = founder.expect(" 324 Founder #transfer ")
            mode_line = next(line for line in modes if " 324 Founder #transfer " in line)
            restored_modes = mode_line.split(" 324 Founder #transfer ", 1)[1].split()[0]
            assert "n" in restored_modes and "t" in restored_modes and "r" in restored_modes, modes
            founder.send("CHANSERV INFO #transfer")
            info = founder.expect("founder=Bob")
            assert any("topiclock=ON" in line and "successor=NONE" in line and
                       "greeting=Bob is founder now" in line for line in info), info

            old_founder = IRCClient(port)
            register(old_founder, "OldFounder")
            old_founder.send("IDENTIFY Alice alicepass")
            old_founder.expect("Password accepted - you are now identified.")
            old_founder.send("JOIN #transfer")
            old_founder.expect(" 366 OldFounder #transfer ")
            old_founder.send("CHANSERV SET #transfer GREETING :Alice still should not be founder")
            old_founder.expect("Only the channel founder may change this setting.")
        finally:
            if founder is not None: founder.close()
            if old_founder is not None: old_founder.close()
            stop(proc)


if __name__ == "__main__":
    main()
