#!/usr/bin/env python3
"""End-to-end WebIRC plus no-spoof/CTCP probe coverage."""
import os,socket,subprocess,sys,tempfile,time
class IRCClient:
    def __init__(self,sock): self.sock=sock;self.sock.settimeout(.25);self.buffer=b""
    def send(self,line): self.sock.sendall((line+"\r\n").encode())
    def expect(self,needle,duration=4.0):
        deadline=time.monotonic()+duration;got=[]
        while time.monotonic()<deadline:
            while b"\n" in self.buffer:
                raw,self.buffer=self.buffer.split(b"\n",1);line=raw.rstrip(b"\r").decode(errors="replace");got.append(line)
                if needle in line:return got
            try:
                data=self.sock.recv(4096)
                if not data:break
                self.buffer+=data
            except socket.timeout:pass
        raise AssertionError(f"expected {needle!r}; got {got!r}")
    def collect(self,duration=.35):
        deadline=time.monotonic()+duration;got=[]
        while time.monotonic()<deadline:
            while b"\n" in self.buffer:
                raw,self.buffer=self.buffer.split(b"\n",1);got.append(raw.rstrip(b"\r").decode(errors="replace"))
            try:
                data=self.sock.recv(4096)
                if not data:break
                self.buffer+=data
            except socket.timeout:pass
        return got
    def close(self):
        try:self.sock.close()
        except OSError:pass
def free_port():
    s=socket.socket(socket.AF_INET,socket.SOCK_STREAM);s.bind(("127.0.0.1",0));p=s.getsockname()[1];s.close();return p
def wait_listen(port,proc):
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        if proc.poll() is not None:raise RuntimeError(proc.stderr.read())
        try:s=socket.create_connection(("127.0.0.1",port),timeout=.1);s.close();return
        except OSError:time.sleep(.05)
    raise RuntimeError("listener did not start")
def main():
    if len(sys.argv)!=2:raise SystemExit("usage: test_webirc.py scratchircd")
    binary=os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-webirc-") as td:
        port=free_port();conf=os.path.join(td,"ircd.conf")
        with open(conf,"w",encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n");f.write("bind_address = 127.0.0.1\n");f.write(f"port = {port}\n");f.write("max_clients = 32\ndns_timeout_seconds = 1\n");f.write("nospoof = yes\nnospoof_timeout_seconds = 5\n");f.write(f"operators_db = {td}/operators.db\n");f.write(f"bans_db = {td}/bans.db\n");f.write("webirc_gateway = 127.0.0.1 gateway-secret\n")
        proc=subprocess.Popen([binary,conf],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);trusted=rejected=silent=None
        try:
            wait_listen(port,proc);trusted=IRCClient(socket.create_connection(("127.0.0.1",port),timeout=3))
            trusted.send("WEBIRC gateway-secret web.example supplied.example 203.0.113.9");trusted.send("NICK webuser");trusted.send("USER webuser 0 * :Web User")
            lines=trusted.expect("PING :")
            ping=next(line for line in lines if line.startswith("PING :"));cookie=ping.split(":",1)[1]
            before_pong=trusted.collect()
            assert not any("\x01VERSION\x01" in line or "\x01WEBSITE\x01" in line for line in before_pong),before_pong
            assert not any(" 001 webuser " in line for line in before_pong),before_pong

            trusted.send("PONG :definitely-wrong")
            after_wrong=trusted.collect()
            assert not any("\x01VERSION\x01" in line or "\x01WEBSITE\x01" in line for line in after_wrong),after_wrong
            assert not any(" 001 webuser " in line for line in after_wrong),after_wrong

            trusted.send(f"PONG :{cookie}")
            version_lines=trusted.expect("\x01VERSION\x01")
            website_lines=trusted.expect("\x01WEBSITE\x01")
            assert any("\x01VERSION\x01" in line for line in version_lines),version_lines
            assert any("\x01WEBSITE\x01" in line for line in website_lines),website_lines
            trusted.expect(" 001 webuser ",duration=5)

            trusted.send("JOIN #blocked");trusted.expect("respond to the CTCP VERSION")
            trusted.send("NOTICE test.local :\x01VERSION TestClient 1.0\x01")
            trusted.send("JOIN #allowed");trusted.expect(" JOIN #allowed")
            trusted.send("NOTICE test.local :\x01WEBSITE https://example.test/client\x01")
            trusted.send("MODE webuser");modes=trusted.expect(" 221 webuser ");assert any("V" in line.rsplit(" ",1)[-1] for line in modes if " 221 webuser " in line),modes

            trusted.send("VERSION")
            server_version=trusted.expect(" 351 webuser ")
            assert any("ScratchIRCd-0.33" in line and "test.local" in line for line in server_version),server_version

            rejected=IRCClient(socket.create_connection(("127.0.0.1",port),timeout=3));rejected.send("WEBIRC wrong-password web.example supplied.example 198.51.100.5");rejected.expect("Unauthorized WEBIRC gateway")

            silent=IRCClient(socket.create_connection(("127.0.0.1",port),timeout=3));silent.send("NICK silentuser");silent.expect("PING :")
            silent.expect("No-spoof PING timeout",duration=8)
        finally:
            if trusted:trusted.close()
            if rejected:rejected.close()
            if silent:silent.close()
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=2)
                except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=2)
    print("WebIRC/no-spoof integration test passed")
if __name__=="__main__":main()
