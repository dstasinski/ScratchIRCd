#!/usr/bin/env python3
"""Dealer-controlled CARDS integration: privacy, ownership, and deck accounting."""
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

class Peer:
    def __init__(self, port, nick):
        self.nick = nick
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.sock.settimeout(.15)
        self.buffer = b""
        self.pending = []
        self.send("NICK " + nick)
        self.send("USER " + nick + " 0 * :" + nick)
        self.expect(" 001 " + nick + " ")
    def send(self, line):
        self.sock.sendall((line + "\r\n").encode())
    def drain(self, duration=.3):
        deadline = time.monotonic() + duration
        lines = self.pending
        self.pending = []
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                lines.append(raw.rstrip(b"\r").decode(errors="replace"))
            try:
                chunk = self.sock.recv(8192)
                if not chunk:
                    break
                self.buffer += chunk
            except socket.timeout:
                pass
        while b"\n" in self.buffer:
            raw, self.buffer = self.buffer.split(b"\n", 1)
            lines.append(raw.rstrip(b"\r").decode(errors="replace"))
        return lines
    def expect(self, needle, duration=4):
        deadline = time.monotonic() + duration
        seen = []
        while time.monotonic() < deadline:
            seen.extend(self.drain(.12))
            for i, line in enumerate(seen):
                if needle in line:
                    self.pending = seen[i+1:] + self.pending
                    return line
        raise AssertionError((self.nick, needle, seen))
    def close(self):
        self.sock.close()

def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratch-cards-") as td:
        port = free_port()
        config = os.path.join(td, "ircd.conf")
        with open(config, "w", encoding="utf-8") as out:
            out.write("server_name = test.local\nnetwork_name = TestNet\n")
            out.write(f"bind_address = 127.0.0.1\nport = {port}\nmax_clients = 16\n")
            out.write("nospoof = no\ndns_timeout_seconds = 1\ngeoip_city_db = \ngeoip_asn_db = \n")
            for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history"):
                out.write(f"{name}_db = {td}/{name}.db\n")
        proc = subprocess.Popen([binary, config], stdout=subprocess.DEVNULL,
                                stderr=subprocess.PIPE, text=True)
        peers = []
        try:
            deadline = time.monotonic() + 5
            while True:
                if proc.poll() is not None:
                    raise RuntimeError(proc.stderr.read())
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=.1):
                        break
                except OSError:
                    if time.monotonic() > deadline:
                        raise RuntimeError("server did not listen")
                    time.sleep(.05)
            for nick in ("Dealer", "Alice", "Bob"):
                peer = Peer(port, nick)
                peers.append(peer)
                peer.send("JOIN #cards")
                peer.expect(" 366 ")
            dealer, alice, bob = peers
            for peer in peers: peer.drain()
            # Invalid deck options must not create a table or consume a slot.
            for bad in ("0", "7", "-1", "2 EXTRA", "JOKERS EXTRA"):
                dealer.send("CARDS NEW #cards " + bad)
                dealer.expect("Usage: CARDS NEW")
            dealer.send("CARDS STATUS #cards")
            dealer.expect("No active card table")
            dealer.send("CARDS NEW #cards 2 JOKERS")
            for peer in peers: peer.expect(" 761 " + peer.nick + " #cards NEW ")
            # A non-dealer cannot reset or close another player's table.
            alice.send("CARDS SHUFFLE #cards")
            alice.expect("Only the dealer")
            alice.send("CARDS END #cards")
            alice.expect("Only the dealer")
            # Rejected dealer actions must not consume cards or alter hands.
            dealer.send("CARDS DEAL #cards Nobody 3")
            dealer.expect("Target must be an online member")
            dealer.send("CARDS DEAL #cards Alice 0")
            dealer.expect("Usage: CARDS DEAL")
            dealer.send("CARDS SHOW #cards 0")
            dealer.expect("Invalid count or insufficient cards")
            dealer.send("CARDS BURN #cards 109")
            dealer.expect("Invalid count or insufficient cards")
            dealer.send("CARDS STATUS #cards")
            dealer.expect("deck: 108 remaining")
            alice.send("CARDS DEAL #cards Alice 2")
            alice.expect("Only the dealer")
            alice.send("CARDS REQUEST #cards 2")
            dealer.expect("Alice requests 2 card(s)")
            alice.expect("Request sent to dealer")
            alice.send("CARDS ASK #cards Alice 7")
            alice.expect("You cannot ask yourself for cards")
            alice.send("CARDS ASK #cards Bob j")
            for peer in peers: peer.expect("asks Bob for rank J")
            alice.send("CARDS ASK #cards Bob 7")
            for peer in peers: peer.expect("asks Bob for rank 7")
            # A request or Go Fish question is informational: neither
            # operation draws cards or creates a player hand.
            dealer.send("CARDS STATUS #cards")
            dealer.expect("deck: 108 remaining")
            alice.send("CARDS HAND #cards")
            alice.expect(" #cards HAND * :(empty)")
            bob.send("CARDS HAND #cards")
            bob.expect(" #cards HAND * :(empty)")
            dealer.send("CARDS DEAL #cards Alice 5")
            alice.expect(" 761 Alice #cards DEAL * :")
            for peer in peers: peer.expect("Alice received 5 card(s)")
            # Only Alice receives the private hand response.
            bob.drain()
            alice.send("CARDS HAND #cards")
            hand = alice.expect(" 761 Alice #cards HAND * :").split(" :")[-1].split()
            assert len(hand) == 5, hand
            assert all(re.fullmatch(r"[A23456789TJQK][SHDC]|JK", x) for x in hand)
            # A public reveal identifies its owner and does not remove cards.
            alice.send("CARDS REVEAL #cards")
            for peer in peers:
                revealed = peer.expect(" #cards REVEAL * :Alice revealed:")
                assert set(hand).issubset(set(revealed.split(" :", 1)[1].split()[2:]))
            alice.send("CARDS HAND #cards")
            assert sorted(alice.expect(" #cards HAND ").split(" :")[-1].split()) == sorted(hand)
            bob.send("CARDS HAND #cards")
            assert "(empty)" in bob.expect(" #cards HAND ")
            bob.send("CARDS PLAY #cards " + hand[0])
            bob.expect("You do not have that card")
            alice.send("CARDS GIVE #cards Bob " + hand[0].lower())
            bob.expect(" #cards GIVE * :" + hand[0])
            for peer in peers: peer.expect("Alice gave 1 card to Bob")
            bob.send("CARDS HAND #cards")
            assert hand[0] in bob.expect(" #cards HAND ")
            alice.send("CARDS HAND #cards")
            assert len(alice.expect(" #cards HAND ").split(" :")[-1].split()) == 4
            bob.send("CARDS DISCARD #cards " + hand[0])
            for peer in peers: peer.expect("Bob discarded 1 card")
            alice.send("CARDS DISCARDS #cards")
            alice.expect("Top: " + hand[0])
            alice.send("CARDS TAKE #cards")
            alice.expect(" #cards TAKE * :" + hand[0])
            for peer in peers: peer.expect("Alice took the top discard")
            alice.send("CARDS MELD #cards " + hand[0] + " " + hand[1])
            for peer in peers: peer.expect("Alice melded")
            dealer.send("CARDS SHOW #cards 2")
            for peer in peers:
                peer.expect(" #cards SHOW ")
                peer.expect(" #cards SHOW ")
            dealer.send("CARDS BURN #cards 1")
            for peer in peers: peer.expect("Dealer burned 1 card")
            dealer.send("CARDS STATUS #cards")
            dealer.expect("deck: 100 remaining")
            dealer.send("CARDS SHUFFLE #cards")
            for peer in peers: peer.expect("shuffled 108 cards")
            alice.send("CARDS HAND #cards")
            alice.expect("(empty)")
            # A rejected meld must not consume any cards.
            alice.send("CARDS HAND #cards")
            assert "(empty)" in alice.expect(" #cards HAND ")
            alice.send("CARDS MELD #cards AS AS")
            alice.expect("Insufficient cards or public table capacity")
            alice.send("CARDS HAND #cards")
            assert "(empty)" in alice.expect(" #cards HAND ")
            dealer.send("CARDS END #cards")
            for peer in peers: peer.expect("closed the card table")

            # Exhaust an entire multi-deck shoe. Every standard card appears
            # twice and the optional joker code appears four times.
            dealer.send("CARDS NEW #cards 2 JOKERS")
            for peer in peers: peer.expect("opened a 2-deck table")
            dealer.send("CARDS DEAL #cards Alice 100")
            deal_reply = alice.expect(" #cards DEAL * :")
            deal_text = deal_reply.split(" :", 1)[1]
            if deal_text != "Cards dealt; use CARDS HAND to see all":
                dealt = deal_text.split()
                assert len(dealt) == 100, (len(dealt), deal_reply)
                assert all(re.fullmatch(r"[A23456789TJQK][SHDC]|JK", code)
                           for code in dealt), deal_reply
            for peer in peers: peer.expect("Alice received 100 card(s)")
            dealer.send("CARDS DEAL #cards Bob 8")
            bob.expect(" #cards DEAL ")
            for peer in peers: peer.expect("Bob received 8 card(s)")
            dealer.send("CARDS DEAL #cards Bob 1")
            dealer.expect("Not enough cards")
            dealer.send("CARDS STATUS #cards")
            dealer.expect("deck: 0 remaining")

            # HAND responses are chunked into bounded IRC lines.
            def full_hand(peer, expected):
                peer.send("CARDS HAND #cards")
                cards = []
                deadline = time.monotonic() + 4
                while time.monotonic() < deadline and len(cards) < expected:
                    for line in peer.drain(.15):
                        if " 761 " + peer.nick + " #cards HAND * :" in line:
                            assert len(line.encode()) <= 510, line
                            cards.extend(line.split(" :", 1)[1].split())
                assert len(cards) == expected, (peer.nick, len(cards), expected)
                return cards

            from collections import Counter
            alice_cards = full_hand(alice, 100)
            bob_cards = full_hand(bob, 8)
            if deal_text != "Cards dealt; use CARDS HAND to see all":
                # The initial private DEAL list must match the actual hand,
                # including multiplicities when two decks contain duplicates.
                assert Counter(dealt) == Counter(alice_cards)
            # The rejected ninth card must not change Bob's existing hand.
            assert Counter(full_hand(bob, 8)) == Counter(bob_cards)
            all_cards = Counter(alice_cards + bob_cards)
            assert all_cards["JK"] == 4, all_cards
            assert len(all_cards) == 53, len(all_cards)
            assert all(all_cards[rank + suit] == 2
                       for rank in "A23456789TJQK" for suit in "SHDC")
            # A two-deck shoe has at most two physical copies of any
            # standard card (jokers have four). Asking to meld three
            # copies of a standard card must fail atomically,
            # without consuming the first one or two matched cards.
            owned = next(code for code in alice_cards if code != "JK")
            alice.send(f"CARDS MELD #cards {owned} {owned} {owned}")
            alice.expect("Meld contains a card not in your hand")
            alice.send("CARDS TABLE #cards")
            alice.expect(" #cards TABLE * :(empty)")
            assert Counter(full_hand(alice, 100)) == Counter(alice_cards)
            dealer.send("CARDS STATUS #cards")
            dealer.expect("deck: 0 remaining")
            dealer.send("CARDS END #cards")
            for peer in peers: peer.expect("closed the card table")
            dealer.send("CARDS NEW #cards")
            for peer in peers: peer.expect("opened a 1-deck table")
            dealer.send("CARDS STATUS #cards")
            dealer.expect("deck: 52 remaining")
            dealer.send("CARDS END #cards")
            for peer in peers: peer.expect("closed the card table")
            # Optional deck count really is optional with JOKERS.
            dealer.send("CARDS NEW #cards JOKERS")
            for peer in peers: peer.expect("opened a 1-deck table (54 cards)")
            dealer.send("CARDS STATUS #cards")
            dealer.expect("deck: 54 remaining")
            dealer.send("CARDS END #cards")
            for peer in peers: peer.expect("closed the card table")
            assert proc.poll() is None
        finally:
            for peer in peers: peer.close()
            if proc.poll() is None:
                proc.terminate()
                try: proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
    print("CARDS integration tests passed")

if __name__ == "__main__":
    main()
