#!/usr/bin/env python3
"""End-to-end IRCv3 negotiation, tags, presence, and response coverage."""
import os, socket, subprocess, sys, tempfile, time

class IRCClient:
    def __init__(self, port):
        self.sock=socket.create_connection(("127.0.0.1",port),timeout=3.0)
        self.sock.settimeout(0.25); self.buf=b""
    def send(self,line): self.sock.sendall((line+"\r\n").encode())
    def expect(self,needle,duration=5.0):
        end=time.monotonic()+duration; got=[]
        while time.monotonic()<end:
            while b"\n" in self.buf:
                raw,self.buf=self.buf.split(b"\n",1)
                line=raw.rstrip(b"\r").decode(errors="replace"); got.append(line)
                if needle in line:return got
            try:
                data=self.sock.recv(4096)
                if not data:break
                self.buf+=data
            except socket.timeout:pass
        raise AssertionError(f"expected {needle!r}; got {got!r}")
    def close(self):
        try:self.sock.close()
        except OSError:pass

def free_port():
    s=socket.socket(); s.bind(("127.0.0.1",0)); p=s.getsockname()[1]; s.close(); return p

def wait_listen(port,proc):
    end=time.monotonic()+5
    while time.monotonic()<end:
        if proc.poll() is not None:raise RuntimeError(proc.stderr.read())
        try:s=socket.create_connection(("127.0.0.1",port),timeout=.1); s.close(); return
        except OSError:time.sleep(.05)
    raise RuntimeError("listener did not start")

def register(c,nick,cap=False):
    if cap:c.send("CAP LS 302")
    c.send(f"NICK {nick}"); c.send(f"USER {nick} 0 * :{nick}")
    if cap:return
    c.expect(f" 001 {nick} ")

def main():
    advertised=("account-notify away-notify batch draft/chathistory "
                "extended-join labeled-response message-tags sasl=PLAIN "
                "server-time")
    requested=advertised.replace("sasl=PLAIN","sasl")
    binary=os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-ircv3-") as td:
        port=free_port(); conf=os.path.join(td,"ircd.conf")
        with open(conf,"w") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\nbind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\nbans_db = {td}/bans.db\nnickserv_db = {td}/nickserv.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
        proc=subprocess.Popen([binary,conf],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        clients=[]
        try:
            wait_listen(port,proc)

            watcher=IRCClient(port); clients.append(watcher); register(watcher,"Watcher",True)
            ls=watcher.expect(" CAP * LS :")
            assert any(x.endswith(" CAP * LS :"+advertised) for x in ls),ls
            watcher.send("CAP REQ :"+requested)
            ack=watcher.expect(" CAP Watcher ACK :"+requested)
            ack_line=next(x for x in ack if " CAP Watcher ACK :"+requested in x)
            assert not ack_line.startswith("@time="),ack_line
            watcher.send("CAP LIST")
            listed=watcher.expect(" CAP Watcher LIST :")
            assert any(x.endswith(" CAP Watcher LIST :"+requested) for x in listed),listed

            # REQ is atomic: one unknown token NAKs the whole request.
            watcher.send("CAP REQ :-account-notify not-a-cap")
            watcher.expect(" CAP Watcher NAK :-account-notify not-a-cap")
            watcher.send("CAP LIST")
            still=watcher.expect(" CAP Watcher LIST :")
            assert any("account-notify" in x for x in still),still

            # labeled-response may only remain active while batch is active.
            watcher.send("CAP REQ :-batch")
            watcher.expect(" CAP Watcher NAK :-batch")

            watcher.send("CAP REQ :-sasl")
            watcher.expect(" CAP Watcher ACK :-sasl")
            watcher.send("CAP LIST")
            after=watcher.expect(" CAP Watcher LIST :")
            assert not any(" sasl" in x for x in after if " CAP Watcher LIST " in x),after
            watcher.send("CAP END")
            welcome=watcher.expect(" 001 Watcher ")
            assert any(x.startswith("@time=") for x in welcome if " 001 Watcher " in x),welcome
            watcher.send("JOIN #captest")
            own_join=watcher.expect(" JOIN #captest * :Watcher")
            assert any(x.startswith("@time=") for x in own_join if " JOIN #captest " in x),own_join

            alice=IRCClient(port); clients.append(alice); register(alice,"Alice")
            alice.send("JOIN #captest"); alice.expect(" JOIN #captest")
            extended=watcher.expect(" JOIN #captest * :Alice")
            assert any(x.startswith("@time=") for x in extended if " JOIN #captest " in x),extended

            alice.send("AWAY :gone for testing"); alice.expect(" 306 Alice ")
            away=watcher.expect(" AWAY :gone for testing")
            assert any(x.startswith("@time=") for x in away if " AWAY " in x),away
            alice.send("AWAY"); alice.expect(" 305 Alice ")
            cleared=watcher.expect(" :Alice!")
            assert any(x.startswith("@time=") and x.endswith(" AWAY") for x in cleared),cleared

            alice.send("NICKSERV REGISTER secretpass")
            alice.expect("Nickname registered and identified.")
            notice=watcher.expect(" ACCOUNT Alice")
            assert any(x.startswith("@time=") and " :Alice!" in x
                       for x in notice if " ACCOUNT Alice" in x),notice

            tagger=IRCClient(port); clients.append(tagger); register(tagger,"Tagger",True)
            tagger.expect(" CAP * LS :")
            tagger.send("CAP REQ :message-tags")
            tagger.expect(" CAP Tagger ACK :message-tags")
            tagger.send("CAP END"); tagger.expect(" 001 Tagger ")
            tagger.send("JOIN #captest"); tagger.expect(" JOIN #captest")
            watcher.expect(" JOIN #captest * :Tagger")

            tagger.send("@+example.test/intent=wave PRIVMSG #captest :tagged hello")
            tagged=watcher.expect(" PRIVMSG #captest :tagged hello")
            assert any(x.startswith("@time=") and ";+example.test/intent=wave " in x
                       for x in tagged if " PRIVMSG #captest " in x),tagged

            # Client tags have their own 4096-byte allowance and therefore do
            # not consume the classic 510-byte IRC message budget.
            long_tag="x"*600
            tagger.send("@+example.test/long="+long_tag+" PRIVMSG #captest :long tag")
            long_delivery=watcher.expect(" PRIVMSG #captest :long tag")
            assert any("+example.test/long="+long_tag in x for x in long_delivery),long_delivery

            tagger.send("@+typing=active TAGMSG #captest")
            tagmsg=watcher.expect(" TAGMSG #captest")
            assert any(";+typing=active " in x for x in tagmsg),tagmsg

            tagger.send("@+too-long="+("z"*4090)+" PING rejected")
            tagger.expect(" 417 Tagger :Input line was too long")
            tagger.send("PING still-connected")
            tagger.expect(" PONG test.local :still-connected")

            watcher.send("@label=probe-1 PING labeled-ping")
            batch_start=watcher.expect(" labeled-response")
            start=next(x for x in batch_start if " labeled-response" in x)
            assert "label=probe-1" in start and " BATCH +" in start,start
            batch_id=start.split(" BATCH +",1)[1].split(" ",1)[0]
            pong=watcher.expect(" PONG test.local :labeled-ping")
            assert any("batch="+batch_id in x and x.startswith("@time=")
                       for x in pong if " PONG test.local :labeled-ping" in x),pong
            watcher.expect(" BATCH -"+batch_id)
        finally:
            for c in clients:c.close()
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=3)
                except subprocess.TimeoutExpired:proc.kill(); proc.wait(timeout=3)

if __name__=="__main__":main()
