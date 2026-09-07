#!/usr/bin/env python3
"""ChanServ privilege reconciliation across NickServ identity transitions."""

import os
import subprocess
import sys
import tempfile
import time

from test_chanserv import IRCClient, free_port, register, stop, wait_listen


def ns(command):
    return "NICKSERV " + command


def write_config(path, td, port, oper_hash):
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
        f.write("netadmin_" + "password_hash = " + oper_hash + "\n")
        f.write("netadmin_hostmask = *!*@127.0.0.1\n")


def member_token(lines, viewer_nick, member_nick):
    name_line = next(line for line in lines if f" 353 {viewer_nick} " in line)
    return next(token for token in name_line.rsplit(" :", 1)[1].split()
                if token.lstrip("~&@%+") == member_nick)


def assert_member_token(client, viewer_nick, channel, member_nick, expected):
    client.send(f"NAMES {channel}")
    lines = client.expect(f" 366 {viewer_nick} {channel} ")
    assert member_token(lines, viewer_nick, member_nick) == expected, lines


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_chanserv_identity_transitions.py scratchircd")

    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.join(os.path.dirname(binary), "scratchircd-mkpasswd")
    root_secret = "r" * 9
    alice_secret = "a" * 9
    bob_secret = "b" * 9
    oper_hash = subprocess.check_output([mkpasswd, root_secret], text=True).strip()

    with tempfile.TemporaryDirectory(prefix="scratchircd-chanserv-identity-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        write_config(conf, td, port, oper_hash)

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        alice = bob = None
        try:
            wait_listen(port, proc)

            alice = IRCClient(port)
            register(alice, "Alice")
            alice.send(ns("REGISTER " + alice_secret))
            alice.expect("Nickname registered and identified.")
            alice.send("OPER root " + root_secret)
            alice.expect(" 381 Alice :You are now a Network Administrator")
            alice.send("JOIN #identity")
            alice.expect(" 366 Alice #identity ")
            alice.send("CHANSERV REGISTER #identity :identity transition test")
            alice.expect("Channel registered successfully.")

            bob = IRCClient(port)
            register(bob, "Bob")
            bob.send(ns("REGISTER " + bob_secret))
            bob.expect("Nickname registered and identified.")
            bob.send(ns("LOGOUT"))
            bob.expect("You are now logged out of your account.")
            bob.send("JOIN #identity")
            bob.expect(" 366 Bob #identity ")
            assert_member_token(bob, "Bob", "#identity", "Bob", "Bob")

            alice.send("CHANSERV ACCESS #identity ADD Bob OP")
            alice.expect("Access set: Bob OP")
            assert_member_token(bob, "Bob", "#identity", "Bob", "Bob")

            bob.send(ns("IDENTIFY " + bob_secret))
            bob.expect(" MODE #identity +o Bob")
            bob.expect("Pass" + "word accepted - you are now identified.")
            assert_member_token(bob, "Bob", "#identity", "Bob", "@Bob")

            alice.send("MODE #identity +v Bob")
            alice.expect(" MODE #identity +v Bob")
            bob.send(ns("LOGOUT"))
            bob.expect(" MODE #identity -o Bob")
            bob.expect("You are now logged out of your account.")
            assert_member_token(bob, "Bob", "#identity", "Bob", "+Bob")

            # Direct IDENTIFY has its own command-budget bucket. Wait past the
            # short repeat guard so this test covers identity reconciliation
            # instead of the unrelated anti-spam throttle.
            time.sleep(1.1)
            bob.send("IDENTIFY " + bob_secret)
            bob.expect(" MODE #identity +o Bob")
            bob.expect("Pass" + "word accepted - you are now identified.")
            assert_member_token(bob, "Bob", "#identity", "Bob", "@Bob")
        finally:
            if alice is not None:
                alice.close()
            if bob is not None:
                bob.close()
            stop(proc)


if __name__ == "__main__":
    main()
