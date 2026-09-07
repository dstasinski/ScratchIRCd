#!/usr/bin/env python3
"""ChanServ successor promotion when founder accounts are removed."""

import os
import subprocess
import sys
import tempfile

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
        raise SystemExit("usage: test_chanserv_successor_promotion.py scratchircd")

    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.join(os.path.dirname(binary), "scratchircd-mkpasswd")
    root_secret = "r" * 9
    alice_secret = "a" * 9
    bob_secret = "b" * 9
    carol_secret = "c" * 9
    oper_hash = subprocess.check_output([mkpasswd, root_secret], text=True).strip()

    with tempfile.TemporaryDirectory(prefix="scratchircd-chanserv-successor-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        write_config(conf, td, port, oper_hash)

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        alice = bob = carol = None
        try:
            wait_listen(port, proc)

            alice = IRCClient(port)
            register(alice, "Alice")
            alice.send(ns("REGISTER " + alice_secret))
            alice.expect("Nickname registered and identified.")
            alice.send("OPER root " + root_secret)
            alice.expect(" 381 Alice :You are now a Network Administrator")

            bob = IRCClient(port)
            register(bob, "Bob")
            bob.send(ns("REGISTER " + bob_secret))
            bob.expect("Nickname registered and identified.")

            carol = IRCClient(port)
            register(carol, "Carol")
            carol.send(ns("REGISTER " + carol_secret))
            carol.expect("Nickname registered and identified.")

            alice.send("JOIN #inherit")
            alice.expect(" 366 Alice #inherit ")
            alice.send("CHANSERV REGISTER #inherit :successor promotion")
            alice.expect("Channel registered successfully.")
            alice.send("CHANSERV SET #inherit SUCCESSOR Bob")
            alice.expect("Successor updated.")
            bob.send("JOIN #inherit")
            bob.expect(" 366 Bob #inherit ")

            alice.send("JOIN #orphan")
            alice.expect(" 366 Alice #orphan ")
            alice.send("CHANSERV REGISTER #orphan :no successor")
            alice.expect("Channel registered successfully.")

            # A temporarily disabled account loses live authority, but its
            # stored successor and access records are intentionally preserved
            # so re-enabling the same account restores the same policy.
            alice.send("JOIN #references")
            alice.expect(" 366 Alice #references ")
            alice.send("CHANSERV REGISTER #references :reference cleanup")
            alice.expect("Channel registered successfully.")
            alice.send("CHANSERV SET #references SUCCESSOR Carol")
            alice.expect("Successor updated.")
            alice.send("CHANSERV ACCESS #references ADD Carol OP")
            alice.expect("Access set: Carol OP")
            carol.send("JOIN #references")
            carol.expect(" 366 Carol #references ")
            assert_member_token(carol, "Carol", "#references", "Carol", "@Carol")

            alice.send("NSSET Carol ENABLED 0")
            carol.expect(" MODE #references -o Carol")
            alice.expect("NickServ account updated.")
            assert_member_token(carol, "Carol", "#references", "Carol", "Carol")
            alice.send("CHANSERV INFO #references")
            disabled_reference_info = alice.expect("successor=Carol")
            assert any("founder=Alice" in line for line in disabled_reference_info), disabled_reference_info
            alice.send("CHANSERV ACCESS #references LIST")
            disabled_access_lines = alice.expect("End of access list for #references.")
            assert any("Carol:3" in line for line in disabled_access_lines), disabled_access_lines

            alice.send("NSSET Carol ENABLED 1")
            alice.expect("NickServ account updated.")
            carol.close(); carol = None
            carol = IRCClient(port)
            register(carol, "Carol")
            carol.send("IDENTIFY Carol " + carol_secret)
            carol.expect("Pass" + "word accepted - you are now identified.")
            carol.send("JOIN #references")
            carol.expect(" 366 Carol #references ")
            assert_member_token(carol, "Carol", "#references", "Carol", "@Carol")

            # A permanently dropped account must not remain as a successor or
            # access holder. Otherwise re-registering the same account name
            # could unexpectedly resurrect old channel authority.
            alice.send("NSDROP Carol")
            carol.expect(" MODE #references -o Carol")
            alice.expect("NickServ account deleted.")
            alice.send("CHANSERV INFO #references")
            reference_info = alice.expect("successor=NONE")
            assert any("founder=Alice" in line for line in reference_info), reference_info
            alice.send("CHANSERV ACCESS #references LIST")
            access_lines = alice.expect("End of access list for #references.")
            assert not any("Carol:" in line for line in access_lines), access_lines

            carol.close(); carol = None
            carol = IRCClient(port)
            register(carol, "Carol")
            carol.send(ns("REGISTER " + carol_secret))
            carol.expect("Nickname registered and identified.")
            carol.send("JOIN #references")
            carol.expect(" 366 Carol #references ")
            assert_member_token(carol, "Carol", "#references", "Carol", "Carol")

            alice.send("NSDROP Alice")
            drop_lines = alice.expect("NickServ account deleted.")
            assert any(" MODE #inherit +qo Bob Bob" in line or
                       " MODE #inherit +oq Bob Bob" in line
                       for line in drop_lines), drop_lines
            assert any(" MODE #orphan -r" in line for line in drop_lines), drop_lines

            bob.send("CHANSERV INFO #inherit")
            inherit_info = bob.expect("founder=Bob")
            assert any("successor=NONE" in line for line in inherit_info), inherit_info
            assert_member_token(bob, "Bob", "#inherit", "Bob", "~Bob")

            bob.send("CHANSERV SET #inherit GREETING :Bob now owns this")
            bob.expect("Greeting updated.")

            bob.send("CHANSERV INFO #orphan")
            bob.expect("Channel is not registered.")
            alice.send("CSINFO #orphan")
            orphan_info = alice.expect("enabled=0")
            assert any("founder=Alice" in line for line in orphan_info), orphan_info

            bob.send("OPER root " + root_secret)
            bob.expect(" 381 Bob :You are now a Network Administrator")
            bob.send("CSSET #orphan ENABLED 1")
            bob.expect("Cannot enable registration until its founder account exists and is enabled.")
            bob.send("CSINFO #orphan")
            orphan_info = bob.expect("enabled=0")
            assert any("founder=Alice" in line for line in orphan_info), orphan_info

            bob.send("CSSET #orphan FOUNDER Bob")
            bob.expect("ChanServ channel updated.")
            bob.send("CSSET #orphan ENABLED 1")
            bob.expect("ChanServ channel updated.")
            alice.expect(" MODE #orphan +r")
            bob.send("CSINFO #orphan")
            orphan_info = bob.expect("enabled=1")
            assert any("founder=Bob" in line for line in orphan_info), orphan_info
        finally:
            if alice is not None:
                alice.close()
            if bob is not None:
                bob.close()
            if carol is not None:
                carol.close()
            stop(proc)


if __name__ == "__main__":
    main()
