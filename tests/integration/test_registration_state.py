#!/usr/bin/env python3
"""Registration state-machine and automatic MOTD integration coverage."""
import os, socket, subprocess, sys, tempfile, time

class IRC:
    def __init__(self, port):
        self.s=socket.create_connection(("127.0.0.1",port),timeout=3); self.s.settimeout(.2); self.b=b""
    def send(self,line): self.s.sendall((line+"\r\n").encode())
    def lines(self,duration=.3):
        end=time.monotonic()+duration; out=[]
        while time.monotonic()<end:
            while b"\n" in self.b:
                raw,self.b=self.b.split(b"\n",1); out.append(raw.rstrip(b"\r").decode(errors="replace"))
            try:
                data=self.s.recv(8192)
                if not data: break
                self.b+=data
            except socket.timeout: pass
        while b"\n" in self.b:
            raw,self.b=self.b.split(b"\n",1); out.append(raw.rstrip(b"\r").decode(errors="replace"))
        return out
    def expect(self,needle,duration=5):
        seen=[]; end=time.monotonic()+duration
        while time.monotonic()<end:
            seen+=self.lines(.1)
            if any(needle in x for x in seen): return seen
        raise AssertionError(f"expected {needle!r}; got {seen!r}")
    def close(self):
        try:self.s.close()
        except OSError:pass

def free_port():
    s=socket.socket(); s.bind(("127.0.0.1",0)); p=s.getsockname()[1]; s.close(); return p

def wait_listen(port,proc):
    end=time.monotonic()+5
    while time.monotonic()<end:
        if proc.poll() is not None: raise RuntimeError(proc.stderr.read())
        try:s=socket.create_connection(("127.0.0.1",port),timeout=.1);s.close();return
        except OSError:time.sleep(.05)
    raise RuntimeError("server did not listen")

def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:proc.wait(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=3)

def write_conf(path,td,port,motd,prefix=""):
    with open(path,"w",encoding="utf-8") as f:
        f.write("server_name = test.local\nnetwork_name = TestNet\nbind_address = 127.0.0.1\n")
        f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\nmotd_file = {motd}\n")
        for name in ("operators","bans","nickserv","chanserv","memoserv","history"):
            f.write(f"{name}_db = {td}/{prefix}{name}.db\n")
        f.write("geoip_city_db = \ngeoip_asn_db = \n")

def main():
    if len(sys.argv)!=2: raise SystemExit("usage: test_registration_state.py scratchircd")
    binary=os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-registration-") as td:
        port=free_port(); conf=os.path.join(td,"ircd.conf"); motd=os.path.join(td,"motd.txt")
        with open(motd,"w",encoding="utf-8") as f:f.write("Registration test MOTD\nSecond line\n")
        write_conf(conf,td,port,motd)
        proc=subprocess.Popen([binary,conf],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        clients=[]
        try:
            wait_listen(port,proc)
            first=IRC(port);clients.append(first)
            first.send("NICK First[");first.send("USER first_user 0 * :First User")
            welcome=first.expect(" 376 First[ ")
            nums=[x.split(" ",3)[1] for x in welcome if x.startswith(":test.local ")]
            for n in ("001","005","375","372","376"):assert n in nums,(n,welcome)
            assert nums.index("001")<nums.index("005")<nums.index("375")<nums.index("372")<nums.index("376"),nums
            assert any("Registration test MOTD" in x for x in welcome),welcome

            before=IRC(port);clients.append(before)
            before.send("USER before 0 * :Before Nick");before.send("NICK BeforeNick")
            before.expect(" 376 BeforeNick ")

            locked=IRC(port);clients.append(locked)
            locked.send("CAP LS 302");locked.expect(" CAP * LS :")
            locked.send("NICK LockedUser");locked.send("USER original 0 * :Original Identity")
            locked.send("USER replaced 0 * :Replaced Identity");locked.expect(" 462 LockedUser ")
            locked.send("CAP END");locked_welcome=locked.expect(" 376 LockedUser ")
            assert any(" 001 LockedUser " in x and "!original@" in x for x in locked_welcome),locked_welcome

            collision=IRC(port);clients.append(collision)
            collision.send("NICK First{");collision.expect(" 433 * First{ ")
            collision.send("NICK RetryNick");collision.send("USER retry 0 * :Retry User")
            collision.expect(" 376 RetryNick ")

            reserved=IRC(port);clients.append(reserved)
            reserved.send("NICK nickserv");reserved.expect(" 437 * nickserv ")
            reserved.send("NICK Ordinary");reserved.send("USER ordinary 0 * :Ordinary User")
            reserved.expect(" 376 Ordinary ")

            first.send("CAP END");first.send("PASS ignored")
            repeated=first.expect(" 462 First[ ",3);repeated+=first.lines(.4)
            assert not any(" 001 First[ " in x for x in repeated),repeated
        finally:
            for c in clients:c.close()
            stop(proc)

        port=free_port();missing=os.path.join(td,"missing.txt");write_conf(conf,td,port,missing,"missing-")
        proc=subprocess.Popen([binary,conf],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        c=None
        try:
            wait_listen(port,proc);c=IRC(port)
            c.send("NICK NoMotd");c.send("USER nomotd 0 * :No MOTD")
            got=c.expect(" 422 NoMotd ");assert any(" 001 NoMotd " in x for x in got),got
            c.send("PING :after-422");c.expect("PONG test.local ::after-422")
        finally:
            if c:c.close()
            stop(proc)
    print("registration state and automatic MOTD tests passed")

if __name__=="__main__":main()
