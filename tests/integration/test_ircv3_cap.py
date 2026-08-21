#!/usr/bin/env python3
"""End-to-end IRCv3 capability bookkeeping and account-notify coverage."""
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
            assert any("account-notify" in x and "sasl" in x for x in ls),ls
            watcher.send("CAP REQ :account-notify sasl")
            watcher.expect(" CAP Watcher ACK :account-notify sasl")
            watcher.send("CAP LIST")
            listed=watcher.expect(" CAP Watcher LIST :")
            assert any("account-notify" in x and "sasl" in x for x in listed),listed

            # REQ is atomic: one unknown token NAKs the whole request.
            watcher.send("CAP REQ :-account-notify not-a-cap")
            watcher.expect(" CAP Watcher NAK :-account-notify not-a-cap")
            watcher.send("CAP LIST")
            still=watcher.expect(" CAP Watcher LIST :")
            assert any("account-notify" in x for x in still),still

            watcher.send("CAP REQ :-sasl")
            watcher.expect(" CAP Watcher ACK :-sasl")
            watcher.send("CAP LIST")
            after=watcher.expect(" CAP Watcher LIST :account-notify")
            assert not any(" sasl" in x for x in after if " CAP Watcher LIST " in x),after
            watcher.send("CAP END"); watcher.expect(" 001 Watcher ")
            watcher.send("JOIN #captest"); watcher.expect(" JOIN #captest")

            alice=IRCClient(port); clients.append(alice); register(alice,"Alice")
            alice.send("JOIN #captest"); alice.expect(" JOIN #captest")
            alice.send("NICKSERV REGISTER secretpass")
            alice.expect("Nickname registered and identified.")
            notice=watcher.expect(" ACCOUNT Alice")
            assert any(x.startswith(":Alice!") for x in notice if " ACCOUNT Alice" in x),notice
        finally:
            for c in clients:c.close()
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=3)
                except subprocess.TimeoutExpired:proc.kill(); proc.wait(timeout=3)

if __name__=="__main__":main()
