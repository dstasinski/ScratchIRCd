#!/usr/bin/env python3
"""End-to-end persistent SQLite CHATHISTORY and server-time coverage."""
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

def register(c,nick):
    c.send(f"NICK {nick}"); c.send(f"USER {nick} 0 * :{nick}"); c.expect(f" 001 {nick} ")

def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:proc.wait(timeout=3)
        except subprocess.TimeoutExpired:proc.kill(); proc.wait(timeout=3)

def main():
    binary=os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-history-") as td:
        port=free_port(); conf=os.path.join(td,"ircd.conf"); history=os.path.join(td,"history.db")
        with open(conf,"w") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\nbind_address = 127.0.0.1\n")
            f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\nhistory_limit = 20\n")
            f.write(f"operators_db = {td}/operators.db\nbans_db = {td}/bans.db\nnickserv_db = {td}/nickserv.db\nhistory_db = {history}\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \n")

        proc=subprocess.Popen([binary,conf],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        sender=None
        try:
            wait_listen(port,proc)
            sender=IRCClient(port); register(sender,"Alice")
            sender.send("JOIN #history"); sender.expect(" JOIN #history")
            sender.send("PRIVMSG #history :persisted one")
            sender.send("NOTICE #history :persisted two")
            time.sleep(.15)
        finally:
            if sender:sender.close()
            stop(proc)

        assert os.path.exists(history), "history database was not created"

        proc=subprocess.Popen([binary,conf],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        reader=None; live=None
        try:
            wait_listen(port,proc)
            reader=IRCClient(port)
            reader.send("CAP LS 302")
            ls=reader.expect(" CAP * LS :")
            assert any("batch" in x and "draft/chathistory" in x and "server-time" in x for x in ls),ls
            reader.send("CAP REQ :batch draft/chathistory server-time")
            reader.expect(" CAP * ACK :batch draft/chathistory server-time")
            reader.send("NICK Reader"); reader.send("USER reader 0 * :Reader")
            reader.send("CAP END"); reader.expect(" 001 Reader ")
            isupport=reader.expect("CHATHISTORY=20")
            assert any("MSGREFTYPES=timestamp" in x for x in isupport),isupport
            reader.send("JOIN #history"); reader.expect(" JOIN #history")
            reader.send("CHATHISTORY LATEST #history * 10")
            lines=reader.expect(" BATCH -")
            history_lines=[x for x in lines if "@batch=" in x]
            assert len(history_lines)==2,lines
            assert "time=" in history_lines[0] and " PRIVMSG #history :persisted one" in history_lines[0],lines
            assert "time=" in history_lines[1] and " NOTICE #history :persisted two" in history_lines[1],lines
            assert any(" BATCH +" in x and " chathistory #history" in x for x in lines),lines

            # server-time also applies to live message delivery, not only replay.
            live=IRCClient(port); register(live,"Bob")
            live.send("JOIN #history"); live.expect(" JOIN #history")
            reader.expect(" JOIN #history")
            live.send("PRIVMSG #history :live timed")
            delivered=reader.expect(" PRIVMSG #history :live timed")
            assert any(x.startswith("@time=") for x in delivered if " PRIVMSG #history :live timed" in x),delivered
        finally:
            if live:live.close()
            if reader:reader.close()
            stop(proc)

if __name__=="__main__":main()
