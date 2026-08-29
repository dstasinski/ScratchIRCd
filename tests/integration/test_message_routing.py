#!/usr/bin/env python3
"""Wire-level PRIVMSG/NOTICE routing and no-spoof CTCP coverage."""
import os, socket, subprocess, sys, tempfile, time

class IRC:
    def __init__(self,port):
        self.s=socket.create_connection(("127.0.0.1",port),timeout=3);self.s.settimeout(.2)
        self.b=b"";self.pending=[]
    def send(self,line):self.s.sendall((line+"\r\n").encode())
    def fill(self):
        while b"\n" in self.b:
            raw,self.b=self.b.split(b"\n",1);self.pending.append(raw.rstrip(b"\r").decode(errors="replace"))
    def expect(self,needle,duration=5):
        end=time.monotonic()+duration;seen=[]
        while time.monotonic()<end:
            self.fill()
            for i,line in enumerate(self.pending):
                if needle in line:
                    del self.pending[:i+1];return line
            seen+=self.pending;self.pending.clear()
            try:
                data=self.s.recv(8192)
                if not data:break
                self.b+=data
            except socket.timeout:pass
        raise AssertionError(f"expected {needle!r}; got {seen!r}")
    def drain(self,duration=.4):
        end=time.monotonic()+duration;out=[]
        while time.monotonic()<end:
            self.fill();out+=self.pending;self.pending.clear()
            try:
                data=self.s.recv(8192)
                if not data:break
                self.b+=data
            except socket.timeout:pass
        self.fill();out+=self.pending;self.pending.clear();return out
    def expect_not(self,needle,duration=.6):
        got=self.drain(duration)
        assert not any(needle in x for x in got),(needle,got)
        return got
    def close(self):
        try:self.s.close()
        except OSError:pass

def free_port():
    s=socket.socket();s.bind(("127.0.0.1",0));p=s.getsockname()[1];s.close();return p

def wait_listen(port,proc):
    end=time.monotonic()+5
    while time.monotonic()<end:
        if proc.poll() is not None:raise RuntimeError(proc.stderr.read())
        try:s=socket.create_connection(("127.0.0.1",port),timeout=.1);s.close();return
        except OSError:time.sleep(.05)
    raise RuntimeError("server did not listen")

def register(c,nick):
    c.send(f"NICK {nick}");c.send(f"USER {nick.lower()} 0 * :{nick} User")
    ping=c.expect("PING :")
    token=ping.rsplit(":",1)[1]
    c.send(f"PONG :{token}")
    c.expect(f"PRIVMSG {nick} :\x01VERSION\x01")
    c.expect(f" 001 {nick} ")
    c.send(f"NOTICE test.local :\x01VERSION RoutingTest 1.0\x01")
    c.drain(.15)

def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:proc.wait(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=3)

def main():
    if len(sys.argv)!=2:raise SystemExit("usage: test_message_routing.py scratchircd")
    binary=os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-routing-") as td:
        port=free_port();conf=os.path.join(td,"ircd.conf");motd=os.path.join(td,"motd.txt")
        with open(motd,"w",encoding="utf-8") as f:f.write("Routing test\n")
        with open(conf,"w",encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\nbind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\nmotd_file = {motd}\n")
            f.write("nospoof = yes\nnospoof_timeout_seconds = 10\n")
            f.write("cloak_key = 0123456789abcdef0123456789abcdef\n")
            for name in ("operators","bans","nickserv","chanserv","memoserv","history"):
                f.write(f"{name}_db = {td}/{name}.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
        proc=subprocess.Popen([binary,conf],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        clients=[]
        try:
            wait_listen(port,proc)

            # NOTICE must remain silent even before registration.
            raw=IRC(port);clients.append(raw);raw.send("NOTICE Nobody :pre-registration")
            assert not any(" 451 " in x for x in raw.drain(.6))

            alice=IRC(port);clients.append(alice);register(alice,"Alice")
            bob=IRC(port);clients.append(bob);register(bob,"Bob")
            outsider=IRC(port);clients.append(outsider);register(outsider,"Outsider")

            # A valid CTCP VERSION NOTICE to the server clears no-spoof message
            # restrictions; case-insensitive lookup relays the canonical nick.
            alice.send("MODE Alice +x");alice.expect(" 221 Alice +x")
            alice.send("PRIVMSG bOb :direct route")
            direct=bob.expect("PRIVMSG Bob :direct route")
            assert direct.startswith(":Alice!alice@"),direct
            assert "@127.0.0.1 " not in direct,direct
            alice.expect_not("respond to the CTCP VERSION")

            # Self delivery occurs once.
            alice.send("PRIVMSG aLiCe :self route")
            self_line=alice.expect("PRIVMSG Alice :self route")
            assert "self route" in self_line
            assert sum("PRIVMSG Alice :self route" in x for x in alice.drain(.4))==0

            alice.send("JOIN #route");alice.expect(" 366 Alice #route ")
            bob.send("JOIN #route");bob.expect(" 366 Bob #route ")
            alice.drain(.2);bob.drain(.2)
            alice.send("PRIVMSG #route :channel route")
            bob.expect("PRIVMSG #route :channel route")
            alice.expect_not("PRIVMSG #route :channel route")

            # External messages work until +n; PRIVMSG then errors while NOTICE
            # rejects silently, and neither rejected message reaches members.
            outsider.send("PRIVMSG #route :external allowed")
            alice.expect("PRIVMSG #route :external allowed")
            bob.expect("PRIVMSG #route :external allowed")
            alice.send("MODE #route +n");bob.expect(" MODE #route +n")
            outsider.send("PRIVMSG #route :external blocked")
            outsider.expect(" 404 Outsider #route ")
            alice.expect_not("external blocked");bob.expect_not("external blocked")
            outsider.send("NOTICE #route :notice blocked")
            outsider.expect_not(" 4")
            alice.expect_not("notice blocked");bob.expect_not("notice blocked")

            # +T rejects CTCP PRIVMSG with 492 and silently rejects CTCP NOTICE.
            bob.send("MODE Bob +T");bob.expect(" 221 Bob +T")
            alice.send("PRIVMSG Bob :\x01VERSION\x01");alice.expect(" 492 Alice :Bob does not accept CTCPs")
            bob.expect_not("PRIVMSG Bob :\x01VERSION\x01")
            alice.send("NOTICE Bob :\x01VERSION reply\x01")
            bob.expect_not("NOTICE Bob :\x01VERSION reply\x01")

            # Unknown NOTICE targets and comma target lists remain silent.
            alice.drain(.2)
            alice.send("NOTICE MissingNick :silent")
            alice.send("NOTICE Bob,MissingNick :silent")
            alice.expect_not(" 401 ");alice.expect_not(" 407 ")
        finally:
            for c in clients:c.close()
            stop(proc)
    print("message routing tests passed")

if __name__=="__main__":main()
