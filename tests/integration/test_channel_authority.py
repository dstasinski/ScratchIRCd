#!/usr/bin/env python3
"""End-to-end channel authority and action restriction tests."""

import os
import socket
import subprocess
import sys
import tempfile
import time

class IRCClient:
    def __init__(self, port):
        self.sock=socket.create_connection(("127.0.0.1",port),timeout=3.0); self.sock.settimeout(0.2); self.buffer=b""; self.pending=[]
    def send(self,line): self.sock.sendall((line+"\r\n").encode())
    def _next(self,deadline):
        if self.pending:return self.pending.pop(0)
        while time.monotonic()<deadline:
            while b"\n" in self.buffer:
                raw,self.buffer=self.buffer.split(b"\n",1); return raw.rstrip(b"\r").decode(errors="replace")
            try:
                data=self.sock.recv(4096)
                if not data:return None
                self.buffer+=data
            except socket.timeout:pass
        return None
    def expect(self,needle,duration=3.0):
        deadline=time.monotonic()+duration; got=[]
        while time.monotonic()<deadline:
            line=self._next(deadline)
            if line is None:continue
            got.append(line)
            if needle in line:return line
        raise AssertionError(f"expected {needle!r}; got {got!r}")
    def expect_not(self,needle,duration=0.5):
        deadline=time.monotonic()+duration; kept=[]
        while time.monotonic()<deadline:
            line=self._next(deadline)
            if line is None:continue
            if needle in line:raise AssertionError(f"unexpected {needle!r}: {line!r}")
            kept.append(line)
        self.pending.extend(kept)
    def close(self):
        try:self.sock.close()
        except OSError:pass

def free_port():
    s=socket.socket(socket.AF_INET,socket.SOCK_STREAM); s.bind(("127.0.0.1",0)); p=s.getsockname()[1]; s.close(); return p

def wait_listen(port,proc):
    deadline=time.monotonic()+5.0
    while time.monotonic()<deadline:
        if proc.poll() is not None:raise RuntimeError(proc.stderr.read())
        try:s=socket.create_connection(("127.0.0.1",port),timeout=0.1); s.close(); return
        except OSError:time.sleep(0.05)
    raise RuntimeError("server did not listen")

def register(c,nick): c.send(f"NICK {nick}"); c.send(f"USER {nick} 0 * :{nick}"); c.expect(f" 001 {nick} ")
def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:proc.wait(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=3)

def main():
    if len(sys.argv)!=2:raise SystemExit("usage: test_channel_authority.py scratchircd")
    binary=os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-authority-") as td:
        port=free_port(); conf=os.path.join(td,"ircd.conf")
        with open(conf,"w",encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n"); f.write("bind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {td}/operators.db\nbans_db = {td}/bans.db\n")
            f.write(f"nickserv_db = {td}/nickserv.db\nchanserv_db = {td}/chanserv.db\n")
            f.write(f"memoserv_db = {td}/memoserv.db\nhistory_db = {td}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")
        proc=subprocess.Popen([binary,conf],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True); clients=[]
        try:
            wait_listen(port,proc)
            owner=IRCClient(port);clients.append(owner);register(owner,"Owner")
            protect=IRCClient(port);clients.append(protect);register(protect,"Protect")
            oper=IRCClient(port);clients.append(oper);register(oper,"ChanOp")
            half=IRCClient(port);clients.append(half);register(half,"Half")
            voice=IRCClient(port);clients.append(voice);register(voice,"Voice")
            outsider=IRCClient(port);clients.append(outsider);register(outsider,"Outside")
            pending=IRCClient(port);clients.append(pending);pending.send("NICK Pending")
            owner.send("JOIN #authority");owner.expect(" 366 Owner #authority ")
            for c,nick in [(protect,"Protect"),(oper,"ChanOp"),(half,"Half"),(voice,"Voice")]:c.send("JOIN #authority");c.expect(f" 366 {nick} #authority ")
            owner.send("MODE #authority +v Voice");voice.expect(" MODE #authority +v Voice")
            owner.send("MODE #authority +h Half");half.expect(" MODE #authority +h Half")
            owner.send("MODE #authority +o ChanOp");oper.expect(" MODE #authority +o ChanOp")
            owner.send("MODE #authority +a Protect");protect.expect(" MODE #authority +a Protect")

            # A legal inbound MODE can still become too long once the sender's
            # nick!user@host prefix is added for channel broadcast. Reject the
            # whole request before any of its mask mutations are applied.
            long_masks=[("a"*116)+str(i)+"!*@*" for i in range(4)]
            oversized="MODE #authority +bbbb "+" ".join(long_masks)
            assert len(oversized.encode()) == 509, len(oversized.encode())
            owner.send(oversized);owner.expect(" 417 Owner MODE ")
            protect.expect_not(" MODE #authority +bbbb ")
            owner.send("MODE #authority b")
            for mask in long_masks: owner.expect_not(mask,duration=0.15)
            owner.expect(" 368 Owner #authority ")

            voice.send("INVITE Outside #authority");voice.expect(" 482 Voice #authority ")
            half.send("INVITE Outside #authority");half.expect(" 341 Half Outside #authority");outsider.expect(" INVITE Outside :#authority")
            protect.send("INVITE Outside #authority");protect.expect(" 341 Protect Outside #authority");outsider.expect(" INVITE Outside :#authority")
            half.send("INVITE Pending #authority");half.expect(" 401 Half Pending :No such nick/channel");pending.expect_not(" INVITE Pending :#authority")

            owner.send("JOIN #invitebound");owner.expect(" 366 Owner #invitebound ");owner.send("MODE #invitebound +i");owner.expect(" MODE #invitebound +i")
            owner.send("INVITE Outside #invitebound");owner.expect(" 341 Owner Outside #invitebound");outsider.expect(" INVITE Outside :#invitebound")
            outsider.send("NICK RenamedOutside");outsider.send("MODE RenamedOutside");outsider.expect(" 221 RenamedOutside ")
            outsider.send("JOIN #invitebound");outsider.expect(" 366 RenamedOutside #invitebound ")
            outsider.send("PART #invitebound :consume invite");outsider.expect(" PART #invitebound :consume invite")
            outsider.send("JOIN #invitebound");outsider.expect(" 473 RenamedOutside #invitebound ")
            owner.send("INVITE RenamedOutside #invitebound");owner.expect(" 341 Owner RenamedOutside #invitebound");outsider.expect(" INVITE RenamedOutside :#invitebound")
            outsider.close();clients.remove(outsider);outsider=IRCClient(port);clients.append(outsider);register(outsider,"RenamedOutside")
            outsider.send("JOIN #invitebound");outsider.expect(" 473 RenamedOutside #invitebound ")

            owner.send("MODE #authority +V");protect.expect(" MODE #authority +V")
            protect.send("INVITE RenamedOutside #authority");protect.expect(" 518 Protect :Cannot invite (+V) at channel #authority");outsider.expect_not(" INVITE RenamedOutside :#authority")
            owner.send("MODE #authority -V");protect.expect(" MODE #authority -V")
            owner.send("MODE #authority +t");voice.expect(" MODE #authority +t")
            voice.send("TOPIC #authority :voice topic");voice.expect(" 482 Voice #authority ")
            half.send("TOPIC #authority :halfop topic");owner.expect(" TOPIC #authority :halfop topic")
            outsider.send("TOPIC #authority");outsider.expect(" 332 RenamedOutside #authority :halfop topic")
            owner.send("MODE #authority +T");protect.expect(" MODE #authority +T")
            voice.send("NOTICE #authority :blocked notice");protect.expect_not("NOTICE #authority :blocked notice")
            owner.send("MODE #authority -T");protect.expect(" MODE #authority -T")
            voice.send("NOTICE #authority :restored notice");protect.expect("NOTICE #authority :restored notice")

            # Test +b/+e speaking on an ordinary member, independently of +o authority.
            owner.send("MODE #authority -o ChanOp");oper.expect(" MODE #authority -o ChanOp")
            owner.send("MODE #authority +b ChanOp!*@*");oper.expect(" MODE #authority +b ChanOp!*@*")
            oper.send("PRIVMSG #authority :blocked by ban");oper.expect(" 404 ChanOp #authority ")
            protect.expect_not("PRIVMSG #authority :blocked by ban")
            oper.send("NOTICE #authority :blocked notice by ban");protect.expect_not("NOTICE #authority :blocked notice by ban")
            owner.send("MODE #authority +e ChanOp!*@*");oper.expect(" MODE #authority +e ChanOp!*@*")
            oper.send("PRIVMSG #authority :exception works");protect.expect("PRIVMSG #authority :exception works")
            owner.send("MODE #authority -e ChanOp!*@*");oper.expect(" MODE #authority -e ChanOp!*@*")
            owner.send("MODE #authority -b ChanOp!*@*");oper.expect(" MODE #authority -b ChanOp!*@*")
            owner.send("MODE #authority +o ChanOp");oper.expect(" MODE #authority +o ChanOp")

            # Mask matching is live against nick!user@display_host, not cached identity.
            owner.send("MODE #authority +b NewVoice!*@*");voice.expect(" MODE #authority +b NewVoice!*@*")
            voice.send("PRIVMSG #authority :before rename");protect.expect("PRIVMSG #authority :before rename")
            voice.send("NICK NewVoice");owner.expect(" NICK :NewVoice")
            voice.send("PRIVMSG #authority :after rename blocked");voice.expect(" 404 NewVoice #authority ")
            protect.expect_not("PRIVMSG #authority :after rename blocked")
            owner.send("MODE #authority -b NewVoice!*@*");voice.expect(" MODE #authority -b NewVoice!*@*")

            # Protected may set a mask matching OWNER, but OWNER remains immune.
            protect.send("MODE #authority +b Owner!*@*");owner.expect(" MODE #authority +b Owner!*@*")
            owner.send("PRIVMSG #authority :owner remains authoritative");protect.expect("PRIVMSG #authority :owner remains authoritative")
            owner.send("MODE #authority -b Owner!*@*");protect.expect(" MODE #authority -b Owner!*@*")

            # +I is a mask exception to +i and does not require an explicit invite.
            owner.send("JOIN #invex");owner.expect(" 366 Owner #invex ");owner.send("MODE #invex +i");owner.expect(" MODE #invex +i")
            owner.send("MODE #invex +I RenamedOutside!*@*");owner.expect(" MODE #invex +I RenamedOutside!*@*")
            outsider.send("JOIN #invex");outsider.expect(" 366 RenamedOutside #invex ")

            # Ban redirects re-run destination policy; destination keys are not bypassed or inherited.
            owner.send("JOIN #redirectkey");owner.expect(" 366 Owner #redirectkey ")
            owner.send("MODE #redirectkey +k destkey");owner.expect(" MODE #redirectkey +k destkey")
            owner.send("JOIN #bansource");owner.expect(" 366 Owner #bansource ")
            owner.send("MODE #bansource +B #redirectkey");owner.expect(" MODE #bansource +B #redirectkey")
            owner.send("MODE #bansource +b RenamedOutside!*@*");owner.expect(" MODE #bansource +b RenamedOutside!*@*")
            outsider.send("JOIN #bansource sourcekey");link=outsider.expect(" 470 RenamedOutside [Link] #bansource ")
            assert "#redirectkey" in link,link
            outsider.expect(" 475 RenamedOutside #redirectkey ")
            outsider.expect_not(" 366 RenamedOutside #redirectkey ")

            # Invite-only policy is also enforced at a redirect destination.
            owner.send("JOIN #redirectinvite");owner.expect(" 366 Owner #redirectinvite ")
            owner.send("MODE #redirectinvite +i");owner.expect(" MODE #redirectinvite +i")
            owner.send("MODE #bansource +B #redirectinvite");owner.expect(" MODE #bansource +B #redirectinvite")
            outsider.send("JOIN #bansource");link=outsider.expect(" 470 RenamedOutside [Link] #bansource ")
            assert "#redirectinvite" in link,link
            outsider.expect(" 473 RenamedOutside #redirectinvite ")

            # +L redirects a full channel with the same Client identity and normal destination JOIN path.
            protect.send("JOIN #limitdest");protect.expect(" 366 Protect #limitdest ")
            owner.send("JOIN #limitsource");owner.expect(" 366 Owner #limitsource ")
            owner.send("MODE #limitsource +lL 1 #limitdest");owner.expect(" MODE #limitsource +lL 1 #limitdest")
            outsider.send("JOIN #limitsource");link=outsider.expect(" 470 RenamedOutside [Link] #limitsource ")
            assert "#limitdest" in link,link
            joined=protect.expect(" JOIN #limitdest")
            assert joined.startswith(":RenamedOutside!RenamedOutside@"),joined
            outsider.expect(" 366 RenamedOutside #limitdest ")

            # Cross-channel redirect loops are bounded by IRC_JOIN_REDIRECT_MAX and never force a JOIN.
            owner.send("JOIN #loopa");owner.expect(" 366 Owner #loopa ")
            owner.send("JOIN #loopb");owner.expect(" 366 Owner #loopb ")
            owner.send("MODE #loopa +B #loopb");owner.expect(" MODE #loopa +B #loopb")
            owner.send("MODE #loopb +B #loopa");owner.expect(" MODE #loopb +B #loopa")
            owner.send("MODE #loopa +b RenamedOutside!*@*");owner.expect(" MODE #loopa +b RenamedOutside!*@*")
            owner.send("MODE #loopb +b RenamedOutside!*@*");owner.expect(" MODE #loopb +b RenamedOutside!*@*")
            outsider.send("JOIN #loopa");outsider.expect(" 474 RenamedOutside #loopa ")
            outsider.expect_not(" 366 RenamedOutside #loopa ")
            outsider.expect_not(" 366 RenamedOutside #loopb ")

            owner.send("MODE #authority +s");protect.expect(" MODE #authority +s")
            outsider.send("LIST");outsider.expect_not(" 322 RenamedOutside #authority ");outsider.expect(" 323 RenamedOutside :End of /LIST")
            outsider.send("TOPIC #authority");outsider.expect(" 403 RenamedOutside #authority ")
            outsider.expect_not("halfop topic")
            owner.send("TOPIC #authority");owner.expect(" 332 Owner #authority :halfop topic")
            outsider.send("JOIN #ephemeral");outsider.expect(" 366 RenamedOutside #ephemeral ")
            outsider.send("PART #ephemeral :gone");outsider.expect(" PART #ephemeral :gone")
            outsider.send("LIST");outsider.expect_not(" 322 RenamedOutside #ephemeral ");outsider.expect(" 323 RenamedOutside :End of /LIST")
        finally:
            for c in clients:c.close()
            stop(proc)

if __name__=="__main__":main()
