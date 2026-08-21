#!/usr/bin/env python3
import os,socket,subprocess,sys,tempfile,time
class C:
 def __init__(self,p): self.s=socket.create_connection(("127.0.0.1",p),3);self.s.settimeout(.2);self.b=b""
 def send(self,x): self.s.sendall((x+"\r\n").encode())
 def expect(self,n,timeout=5):
  end=time.time()+timeout;got=[]
  while time.time()<end:
   try:self.b+=self.s.recv(4096)
   except socket.timeout:pass
   while b"\n" in self.b:
    l,self.b=self.b.split(b"\n",1);t=l.rstrip(b"\r").decode(errors="replace");got.append(t)
    if n in t:return t
  raise AssertionError(f"expected {n!r}; got {got!r}")
 def close(self): self.s.close()
def freeport():s=socket.socket();s.bind(("127.0.0.1",0));p=s.getsockname()[1];s.close();return p
def start(bin,d,p):
 conf=os.path.join(d,"ircd.conf");open(conf,"w").write(f"server_name = test.local\nnetwork_name = Test\nbind_address = 127.0.0.1\nport = {p}\nmax_clients = 32\ndns_timeout_seconds = 1\noperators_db = {d}/operators.db\nbans_db = {d}/bans.db\nnickserv_db = {d}/nickserv.db\nchanserv_db = {d}/chanserv.db\nhistory_db = {d}/history.db\ngeoip_city_db = {d}/none-city.mmdb\ngeoip_asn_db = {d}/none-asn.mmdb\n")
 q=subprocess.Popen([bin,conf],cwd=d,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 for _ in range(40):
  try:s=socket.create_connection(("127.0.0.1",p),.1);s.close();return q
  except OSError:time.sleep(.05)
 raise RuntimeError("server did not start")
def reg(c,n):c.send(f"NICK {n}");c.send(f"USER {n} 0 * :{n}");c.expect(" 001 ")
def main():
 bin=sys.argv[1]
 with tempfile.TemporaryDirectory() as d:
  p=freeport();q=start(bin,d,p)
  a=C(p);reg(a,"Alice");a.send("NICKSERV REGISTER secretpass");a.expect("registered")
  a.send("JOIN #persist");a.expect(" JOIN #persist")
  a.send("CHANSERV REGISTER #persist :Persistent test channel");a.expect("Channel registered")
  a.send("CHANSERV INFO #persist");a.expect("founder=Alice")
  a.send("PART #persist");a.expect(" PART #persist")
  a.send("JOIN #persist");a.expect(" JOIN #persist");a.send("MODE #persist");a.expect(" +r")
  a.close();q.terminate();q.wait(timeout=5)
  q=start(bin,d,p);b=C(p);reg(b,"Alice2");b.send("IDENTIFY Alice secretpass");b.expect("identified")
  b.send("JOIN #persist");b.expect(" JOIN #persist");b.send("NAMES #persist");b.expect("~Alice2")
  b.send("CHANSERV DROP #persist");b.expect("dropped")
  b.close();q.terminate();q.wait(timeout=5)
if __name__=="__main__":main()
