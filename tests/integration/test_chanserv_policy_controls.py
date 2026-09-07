#!/usr/bin/env python3
"""Focused end-to-end coverage for newer ChanServ policy controls.

This test is intentionally standalone so it can be run directly against an
already-built scratchircd binary without changing the normal CTest inventory.
"""

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


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_chanserv_policy_controls.py scratchircd")

    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.join(os.path.dirname(binary), "scratchircd-mkpasswd")
    admin_hash = subprocess.check_output([mkpasswd, "adminpass"], text=True).strip()

    with tempfile.TemporaryDirectory(prefix="scratchircd-chanserv-policy-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        write_config(conf, td, port, admin_hash)

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        alice = bob = carol = guest = None
        try:
            wait_listen(port, proc)

            alice = IRCClient(port)
            register(alice, "Alice")
            alice.send("NICKSERV REGISTER chanpass")
            alice.expect("Nickname registered and identified.")
            alice.send("OPER root adminpass")
            alice.expect(" 381 Alice :You are now a Network Administrator")

            bob = IRCClient(port)
            register(bob, "Bob")
            bob.send("NICKSERV REGISTER bobpass")
            bob.expect("Nickname registered and identified.")

            carol = IRCClient(port)
            register(carol, "Carol")
            carol.send("NICKSERV REGISTER carolpass")
            carol.expect("Nickname registered and identified.")

            alice.send("JOIN #policy")
            alice.expect(" 366 Alice #policy ")
            alice.send("CHANSERV REGISTER #policy :Policy controls")
            alice.expect("Channel registered successfully.")

            alice.send("CHANSERV ACCESS #policy ADD Bob OP")
            alice.expect("Access set: Bob OP")

            bob.send("JOIN #policy")
            bob.expect(" 366 Bob #policy ")
            carol.send("JOIN #policy")
            carol.expect(" 366 Carol #policy ")

            # TOPICLOCK must update live +t immediately and persist through
            # the ordinary MLOCK storage path.
            alice.send("CHANSERV SET #policy TOPICLOCK ON")
            topiclock = alice.expect("Topic locking updated.")
            assert any(" MODE #policy +t" in line for line in topiclock), topiclock
            carol.send("TOPIC #policy :not allowed")
            carol.expect(" 482 Carol #policy ")
            bob.send("TOPIC #policy :operator topic")
            bob.expect(" TOPIC #policy :operator topic")

            # SUCCESSOR accepts an existing enabled account other than the
            # founder and INFO must report the stored account.
            alice.send("CHANSERV SET #policy SUCCESSOR Alice")
            alice.expect("SUCCESSOR must name an existing account other than the founder, or NONE.")
            alice.send("CHANSERV SET #policy SUCCESSOR Carol")
            alice.expect("Successor updated.")
            alice.send("CHANSERV INFO #policy")
            alice.expect("successor=Carol")

            # GREETING is cached into the live channel and delivered after a
            # successful JOIN without ChanServ becoming a channel member.
            alice.send("CHANSERV SET #policy GREETING :Welcome to #policy")
            alice.expect("Greeting updated.")
            guest = IRCClient(port)
            register(guest, "Guest")
            guest.send("JOIN #policy")
            guest.expect(":ChanServ!service@test.local NOTICE Guest :Welcome to #policy")

            # SecureOps audits existing manual +q/+a/+o/+h grants when it is
            # enabled, rejects later unauthorized grants, and leaves +v alone.
            alice.send("MODE #policy +ov Guest Guest")
            alice.expect(" MODE #policy +ov Guest Guest")
            alice.send("CHANSERV SET #policy SECUREOPS ON")
            secureops = alice.expect("SecureOps enabled.")
            assert any(" MODE #policy -o Guest" in line for line in secureops), secureops
            alice.send("MODE #policy +o Guest")
            alice.expect(" 482 Alice #policy ")
            guest.send("NAMES #policy")
            names = guest.expect(" 366 Guest #policy ")
            name_line = next(line for line in names if " 353 Guest " in line)
            guest_token = next(token for token in name_line.rsplit(" :", 1)[1].split()
                               if token.lstrip("~&@%+") == "Guest")
            assert guest_token == "+Guest", names

            alice.send("CHANSERV INFO #policy")
            info = alice.expect("secureops=ON")
            assert any("topiclock=ON" in line and "successor=Carol" in line and
                       "greeting=Welcome to #policy" in line for line in info), info

            alice.close(); alice = None
            bob.close(); bob = None
            carol.close(); carol = None
            guest.close(); guest = None
        finally:
            if alice is not None: alice.close()
            if bob is not None: bob.close()
            if carol is not None: carol.close()
            if guest is not None: guest.close()
            stop(proc)

        # Restart to verify that the newer policy controls survive SQLite
        # persistence and are restored into a newly-created live channel.
        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        founder = guest = None
        try:
            wait_listen(port, proc)
            founder = IRCClient(port)
            register(founder, "Founder")
            founder.send("IDENTIFY Alice chanpass")
            founder.expect("Password accepted - you are now identified.")
            founder.send("JOIN #policy")
            join_lines = founder.expect(" 366 Founder #policy ")
            assert any("Welcome to #policy" in line and "NOTICE Founder" in line
                       for line in join_lines), join_lines
            founder.send("MODE #policy")
            modes = founder.expect(" 324 Founder #policy ")
            mode_line = next(line for line in modes if " 324 Founder #policy " in line)
            assert "t" in mode_line.split(" 324 Founder #policy ", 1)[1].split()[0], modes
            founder.send("CHANSERV INFO #policy")
            info = founder.expect("secureops=ON")
            assert any("topiclock=ON" in line and "successor=Carol" in line and
                       "greeting=Welcome to #policy" in line for line in info), info

            guest = IRCClient(port)
            register(guest, "Guest2")
            guest.send("JOIN #policy")
            guest.expect(":ChanServ!service@test.local NOTICE Guest2 :Welcome to #policy")
            founder.send("MODE #policy +o Guest2")
            founder.expect(" 482 Founder #policy ")
        finally:
            if founder is not None: founder.close()
            if guest is not None: guest.close()
            stop(proc)


if __name__ == "__main__":
    main()
