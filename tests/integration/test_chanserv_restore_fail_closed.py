#!/usr/bin/env python3
"""Registered channels fail closed when persisted runtime state is corrupt."""

import os
import sqlite3
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
        f.write(f"port = {port}\nmax_clients = 16\ndns_timeout_seconds = 1\n")
        f.write(f"operators_db = {td}/operators.db\n")
        f.write(f"bans_db = {td}/bans.db\n")
        f.write(f"nickserv_db = {td}/nickserv.db\n")
        f.write(f"chanserv_db = {td}/chanserv.db\n")
        f.write(f"history_db = {td}/history.db\n")
        f.write("geoip_city_db = \ngeoip_asn_db = \n")
        f.write("netadmin_name = root\n")
        f.write("netadmin_" + "password_hash = " + oper_hash + "\n")
        f.write("netadmin_hostmask = *!*@127.0.0.1\n")


def corrupt_persisted_mask(chanserv_db):
    db = sqlite3.connect(chanserv_db)
    try:
        db.execute(
            "INSERT INTO channel_masks(channel,type,mask,protected_authorized) "
            "VALUES(?1,?2,?3,?4)",
            ("#restorefail", 1, "bad\nmask", 0),
        )
        db.commit()
    finally:
        db.close()


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_chanserv_restore_fail_closed.py scratchircd")

    binary = os.path.abspath(sys.argv[1])
    mkpasswd = os.path.join(os.path.dirname(binary), "scratchircd-mkpasswd")
    root_secret = "r" * 9
    alice_secret = "a" * 9
    oper_hash = subprocess.check_output([mkpasswd, root_secret], text=True).strip()

    with tempfile.TemporaryDirectory(prefix="scratchircd-chanserv-restore-fail-") as td:
        port = free_port()
        conf = os.path.join(td, "ircd.conf")
        chanserv_db = os.path.join(td, "chanserv.db")
        write_config(conf, td, port, oper_hash)

        proc = subprocess.Popen([binary, conf], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        alice = intruder = None
        try:
            wait_listen(port, proc)

            alice = IRCClient(port)
            register(alice, "Alice")
            alice.send(ns("REGISTER " + alice_secret))
            alice.expect("Nickname registered and identified.")
            alice.send("OPER root " + root_secret)
            alice.expect(" 381 Alice :You are now a Network Administrator")

            alice.send("JOIN #restorefail")
            alice.expect(" 366 Alice #restorefail ")
            alice.send("CHANSERV REGISTER #restorefail :restore failure guard")
            alice.expect("Channel registered successfully.")
            alice.send("MODE #restorefail +b *!*@*")
            alice.expect(" MODE #restorefail +b *!*@*")
            alice.send("PART #restorefail")
            alice.expect(" PART #restorefail ")
            alice.close()
            alice = None

            corrupt_persisted_mask(chanserv_db)

            intruder = IRCClient(port)
            register(intruder, "Intruder")
            intruder.send("JOIN #restorefail")
            lines = intruder.expect(
                "Cannot join #restorefail: persistent channel state could not be restored."
            )
            assert not any(" JOIN #restorefail" in line for line in lines), lines
            intruder.send("NAMES #restorefail")
            intruder.expect(" 403 Intruder #restorefail :No such channel")
        finally:
            if alice is not None:
                alice.close()
            if intruder is not None:
                intruder.close()
            stop(proc)


if __name__ == "__main__":
    main()
