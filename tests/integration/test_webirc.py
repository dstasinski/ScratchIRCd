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
def complete_nospoof(client,nick,version="DirectClient 1.0"):
    lines=client.expect("PING :")
    ping=next(line for line in lines if line.startswith("PING :"));cookie=ping.split(":",1)[1]
    client.send(f"PONG :{cookie}")
    client.expect("\x01VERSION\x01")
    client.send(f"NOTICE test.local :\x01VERSION {version}\x01")
    client.expect(f" 001 {nick} ",duration=5)
    return cookie
def main():
    if len(sys.argv)!=3:raise SystemExit("usage: test_webirc.py scratchircd scratchircd-mkpasswd")
    binary=os.path.abspath(sys.argv[1]);mkpasswd=os.path.abspath(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="scratchircd-webirc-") as td:
        port=free_port();conf=os.path.join(td,"ircd.conf")
        admin_hash=subprocess.check_output([mkpasswd,"adminpass"],text=True).strip()
        with open(conf,"w",encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n");f.write("bind_address = 127.0.0.1\n");f.write(f"port = {port}\n");f.write("max_clients = 32\ndns_timeout_seconds = 1\n");f.write("nospoof = yes\nnospoof_timeout_seconds = 5\n");f.write(f"operators_db = {td}/operators.db\n");f.write(f"bans_db = {td}/bans.db\n");f.write("webirc_gateway = 127.0.0.1 gateway-secret\n");f.write("netadmin_name = root\n");f.write(f"netadmin_password_hash = {admin_hash}\n");f.write("netadmin_hostmask = *!*@203.0.113.9\n")
        proc=subprocess.Popen([binary,conf],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);trusted=observer=rejected=silent=None
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

            observer=IRCClient(socket.create_connection(("127.0.0.1",port),timeout=3))
            observer.send("NICK observer");observer.send("USER observer 0 * :Observer User")
            complete_nospoof(observer,"observer","DirectClient 2.0")

            observer.send("WHOIS webuser")
            ordinary_whois=observer.expect(" 318 observer webuser ")
            assert not any(" 672 observer webuser " in line for line in ordinary_whois),ordinary_whois
            assert not any(" 673 observer webuser " in line for line in ordinary_whois),ordinary_whois

            trusted.send("OPER root adminpass")
            trusted.expect(" 381 webuser :You are now a Network Administrator")
            trusted.send("WHOIS webuser")
            oper_web_whois=trusted.expect(" 318 webuser webuser ")
            assert any(" 672 webuser webuser :TestClient 1.0" in line for line in oper_web_whois),oper_web_whois
            assert any(" 673 webuser webuser :https://example.test/client" in line for line in oper_web_whois),oper_web_whois

            trusted.send("WHOIS observer")
            oper_direct_whois=trusted.expect(" 318 webuser observer ")
            assert any(" 672 webuser observer :DirectClient 2.0" in line for line in oper_direct_whois),oper_direct_whois
            assert not any(" 673 webuser observer " in line for line in oper_direct_whois),oper_direct_whois

            trusted.send("VERSION")
            server_version=trusted.expect(" 351 webuser ")
            assert any("ScratchIRCd-0.33" in line and "test.local" in line for line in server_version),server_version

            trusted.send("TIME")
            server_time=trusted.expect(" 391 webuser test.local :")
            assert any("Unknown server time" not in line for line in server_time if " 391 webuser test.local :" in line),server_time

            trusted.send("INFO")
            server_info=trusted.expect(" 374 webuser :End of /INFO list.")
            assert any(" 373 webuser :Server INFO" in line for line in server_info),server_info
            assert any(" 371 webuser :ScratchIRCd ScratchIRCd-0.33 on test.local" in line for line in server_info),server_info
            assert any("virtual services" in line for line in server_info),server_info

            trusted.send("LINKS")
            links=trusted.expect(" 365 webuser * :End of /LINKS list.")
            link_lines=[line for line in links if " 364 webuser " in line]
            assert len(link_lines)==1,links
            assert " 364 webuser test.local test.local :0 ScratchIRCd single-server daemon" in link_lines[0],links

            trusted.send("STATS u")
            stats=trusted.expect(" 219 webuser u :End of /STATS report")
            assert any(" 242 webuser :Server Up " in line for line in stats),stats

            rejected=IRCClient(socket.create_connection(("127.0.0.1",port),timeout=3));rejected.send("WEBIRC wrong-password web.example supplied.example 198.51.100.5");rejected.expect("Unauthorized WEBIRC gateway")

            silent=IRCClient(socket.create_connection(("127.0.0.1",port),timeout=3));silent.send("NICK silentuser");silent.expect("PING :")
            silent.expect("No-spoof PING timeout",duration=8)
        finally:
            if trusted:trusted.close()
            if observer:observer.close()
            if rejected:rejected.close()
            if silent:silent.close()
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=2)
                except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=2)
    print("WebIRC/no-spoof integration test passed")
if __name__=="__main__":main()
