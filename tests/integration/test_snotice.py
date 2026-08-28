#!/usr/bin/env python3
import os,socket,subprocess,sys,tempfile,time

class IRC:
    def __init__(self,port):
        self.s=socket.create_connection(("127.0.0.1",port),timeout=3);self.s.settimeout(.2);self.b=b""
    def send(self,x):self.s.sendall((x+"\r\n").encode())
    def raw(self,b):self.s.sendall(b)
    def lines(self,d=.8):
        end=time.monotonic()+d;out=[]
        while time.monotonic()<end:
            while b"\n" in self.b:
                r,self.b=self.b.split(b"\n",1);out.append(r.rstrip(b"\r").decode(errors="replace"))
            try:
                x=self.s.recv(4096)
                if not x:break
                self.b+=x
            except socket.timeout:pass
        while b"\n" in self.b:
            r,self.b=self.b.split(b"\n",1);out.append(r.rstrip(b"\r").decode(errors="replace"))
        return out
    def expect(self,n,d=4):
        end=time.monotonic()+d;got=[]
        while time.monotonic()<end:
            got+=self.lines(.2)
            for x in got:
                if n in x:return x
        raise AssertionError(f"expected {n!r}; got {got!r}")
    def close(self):self.s.close()

def port():
    s=socket.socket();s.bind(("127.0.0.1",0));p=s.getsockname()[1];s.close();return p

def main():
    if len(sys.argv)!=3:raise SystemExit("usage: test_snotice.py scratchircd scratchircd-mkpasswd")
    binary=os.path.abspath(sys.argv[1]);mk=os.path.abspath(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="scratchircd-snotice-") as td:
        p=port();h=subprocess.check_output([mk,"adminpass"],text=True).strip();conf=os.path.join(td,"ircd.conf")
        with open(conf,"w") as f:
            f.write(f"server_name = test.local\nnetwork_name = TestNet\nbind_address = 127.0.0.1\nport = {p}\nmax_clients = 20\ndns_timeout_seconds = 1\noperators_db = {td}/operators.db\nbans_db = {td}/bans.db\nnickserv_db = {td}/nickserv.db\nchanserv_db = {td}/chanserv.db\nmemoserv_db = {td}/memoserv.db\nhistory_db = {td}/history.db\ngeoip_city_db = \ngeoip_asn_db = \nnetadmin_name = root\nnetadmin_password_hash = {h}\nnetadmin_hostmask = *!*@127.0.0.1\n")
        proc=subprocess.Popen([binary,conf],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);admin=other=newbie=bad=framing=longvictim=None
        try:
            end=time.monotonic()+5
            while time.monotonic()<end:
                if proc.poll() is not None:raise RuntimeError(proc.stderr.read())
                try:s=socket.create_connection(("127.0.0.1",p),timeout=.1);s.close();break
                except OSError:time.sleep(.05)
            admin=IRC(p);admin.send("NICK Admin");admin.send("USER admin 0 * :Admin");admin.expect(" 001 Admin ");admin.send("OPER root adminpass");admin.expect(" 381 Admin ")
            admin.send("SNOTICE +*");admin.expect("SNOTICE mask now: +cokbgwdsavrxmf")
            other=IRC(p)
            admin.expect("Client connection accepted: real_ip=127.0.0.1")
            other.send("NICK Other");other.send("USER other 0 * :Other");other.expect(" 001 Other ")
            admin.expect("Client registered: Other!")
            other.send("SNOTICE +o");other.expect(" 481 Other ")
            other.send("OPER root wrong");admin.expect("Failed OPER:")
            admin.send("SNOTICE -o");admin.expect("SNOTICE mask now:")
            other.send("OPER root wrong")
            assert not any("Failed OPER:" in x for x in admin.lines(1.0)),"operator notice ignored -o mask"
            admin.send("SNOTICE +o");admin.expect("SNOTICE mask now:")
            other.send("OPER root wrong");admin.expect("Failed OPER:")
            admin.send("MODE Admin -s")
            mode_line=admin.expect(" 221 Admin ")
            mode_token=mode_line.split(" 221 Admin ",1)[1].split()[0]
            assert "s" not in mode_token.lstrip("+"),mode_line
            other.send("OPER root wrong")
            assert not any("Failed OPER:" in x for x in admin.lines(1.0)),"SNOTICE delivered with user mode -s"

            admin.send("SNOTICE +cdsvrxm");admin.expect("SNOTICE mask now:")
            other.send("NICKSERV REGISTER service-secret")
            other.expect("Nickname registered and identified")
            reg_notice=admin.expect("NickServ registration: account=Other")
            assert "service-secret" not in reg_notice,reg_notice

            # ChanServ REGISTER is intentionally network-admin-only. Identify
            # the already-opered Admin account, then have the netadmin create
            # the registration so this test continues to cover ChanServ's
            # registration SNOTICE without depending on the retired founder
            # self-registration policy. MODE -s above deliberately disabled
            # delivery, so restore +s before checking Admin's own service event.
            admin.send("MODE Admin +s")
            admin.send("MODE Admin")
            mode_line=admin.expect(" 221 Admin ")
            mode_token=mode_line.split(" 221 Admin ",1)[1].split()[0]
            assert "s" in mode_token.lstrip("+"),mode_line

            # A user-derived operator event may be longer than one IRC NOTICE
            # after the server adds its prefix. It must be chunked, not dropped.
            longvictim=IRC(p);longvictim.send("NICK LongVictim");longvictim.send("USER longvictim 0 * :Long Victim");longvictim.expect(" 001 LongVictim ")
            admin.lines(.5)
            long_reason="R"*400
            admin.send("KILL LongVictim :"+long_reason)
            notice_lines=admin.lines(1.5)
            notice_prefix=":test.local NOTICE Admin :*** "
            notice_payloads=[x[len(notice_prefix):] for x in notice_lines if x.startswith(notice_prefix)]
            kill_start=next((i for i,x in enumerate(notice_payloads) if x.startswith("KILL by Admin")),None)
            assert kill_start is not None,notice_lines
            assert long_reason in "".join(notice_payloads[kill_start:]),notice_payloads
            assert all(len(x.encode())<=510 for x in notice_lines),notice_lines
            longvictim.close();longvictim=None

            # Success and the self-generated service SNOTICE can arrive in the
            # same socket read. Collect them together instead of using two
            # sequential expect() calls, whose helper intentionally consumes
            # all complete lines in each read batch.
            admin.send("NICKSERV REGISTER admin-service-secret")
            registration_lines=admin.lines(1.5)
            assert any("Nickname registered and identified" in x for x in registration_lines),registration_lines
            admin_reg_notice=next((x for x in registration_lines if "NickServ registration: account=Admin" in x),None)
            assert admin_reg_notice is not None,registration_lines
            assert "admin-service-secret" not in admin_reg_notice,admin_reg_notice

            admin.send("JOIN #svc")
            admin.expect(" JOIN #svc")
            other.send("JOIN #svc")
            other.expect(" JOIN #svc")
            admin.send("CHANSERV REGISTER #svc :SNOTICE test")
            channel_registration_lines=admin.lines(1.5)
            assert any("Channel registered successfully" in x for x in channel_registration_lines),channel_registration_lines
            assert any("ChanServ registration: channel=#svc founder=Admin" in x for x in channel_registration_lines),channel_registration_lines

            admin.send("SETHOST Other vhost.example")
            admin.expect("SETHOST by Admin: Other")
            admin.send("DEAF +Other")
            admin.expect("DEAF by Admin: +Other")
            admin.send("NSSET Other ENABLED 1")
            admin.expect("NSSET by Admin: account=Other field=ENABLED value=1")

            newbie=IRC(p);newbie.send("NICK Newbie");newbie.send("USER newbie 0 * :Newbie");newbie.expect(" 001 Newbie ")
            admin.expect("Client registered: Newbie!")
            newbie.send("QUIT :bye")
            admin.expect("Client disconnect: nick=Newbie")
            newbie.close();newbie=None

            bad=IRC(p);bad.send(":spoofed.example PING :x")
            admin.expect("Protocol violation: client-supplied prefix")
            bad.close();bad=None

            framing=IRC(p);framing.raw((b"X"*511)+b"\r\n")
            admin.expect("Protocol violation from 127.0.0.1: malformed or overlong IRC framing")
        finally:
            for c in (admin,other,newbie,bad,framing,longvictim):
                if c:
                    try:c.close()
                    except OSError:pass
            if proc.poll() is None:proc.terminate()
            try:proc.wait(timeout=3)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()

if __name__=="__main__":main()
