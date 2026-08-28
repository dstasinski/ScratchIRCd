#!/usr/bin/env python3
"""Persistence coverage for ChanServ-controlled durable channel logging."""

import os
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time

class IRCClient:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3.0)
        self.sock.settimeout(0.2); self.buffer = b""
    def send(self, line): self.sock.sendall((line + "\r\n").encode())
    def expect(self, needle, duration=4.0):
        deadline=time.monotonic()+duration; lines=[]
        while time.monotonic()<deadline:
            while b"\n" in self.buffer:
                raw,self.buffer=self.buffer.split(b"\n",1)
                line=raw.rstrip(b"\r").decode(errors="replace"); lines.append(line)
                if needle in line: return lines
            try:
                data=self.sock.recv(4096)
                if not data: break
                self.buffer+=data
            except socket.timeout: pass
        raise AssertionError(f"expected {needle!r}; got {lines!r}")
    def close(self):
        try: self.sock.close()
        except OSError: pass

def free_port():
    s=socket.socket(socket.AF_INET,socket.SOCK_STREAM); s.bind(("127.0.0.1",0)); p=s.getsockname()[1]; s.close(); return p

def wait_listen(port,proc):
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        if proc.poll() is not None: raise RuntimeError(proc.stderr.read())
        try:
            s=socket.create_connection(("127.0.0.1",port),timeout=.1); s.close(); return
        except OSError: time.sleep(.05)
    raise RuntimeError("server did not listen")

def register(c,nick):
    c.send(f"NICK {nick}"); c.send(f"USER {nick} 0 * :{nick} User"); c.expect(f" 001 {nick} ")

def start_server(binary,config,cwd):
    return subprocess.Popen([binary,config],cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)

def stop_server(proc):
    if proc.poll() is not None:return
    proc.terminate()
    try: proc.wait(timeout=3)
    except subprocess.TimeoutExpired: proc.kill(); proc.wait(timeout=3)

def wait_queue_row(db_path, text, duration=3.0):
    deadline=time.monotonic()+duration
    count=0
    while time.monotonic()<deadline:
        try:
            with sqlite3.connect(db_path) as db:
                count=db.execute("SELECT COUNT(*) FROM channel_log_queue WHERE body LIKE ?", (f"%{text}%",)).fetchone()[0]
        except sqlite3.OperationalError:
            count=0
        if count == 1:return count
        time.sleep(.02)
    return count

def main():
    if len(sys.argv)!=3: raise SystemExit("usage: test_channel_logging.py scratchircd scratchircd-mkpasswd")
    binary=os.path.abspath(sys.argv[1]); mkpasswd=os.path.abspath(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="scratchircd-channel-logging-") as tmp:
        data=os.path.join(tmp,"data"); os.makedirs(data,exist_ok=True); port=free_port(); conf=os.path.join(tmp,"ircd.conf")
        db_path=os.path.join(data,"chanserv.db")
        admin_hash=subprocess.check_output([mkpasswd,"adminpass"],text=True).strip()
        with open(conf,"w",encoding="utf-8") as f:
            f.write("server_name = test.local\nnetwork_name = TestNet\n")
            f.write("bind_address = 127.0.0.1\n"); f.write(f"port = {port}\nmax_clients = 32\ndns_timeout_seconds = 1\n")
            f.write(f"operators_db = {data}/operators.db\nbans_db = {data}/bans.db\nnickserv_db = {data}/nickserv.db\n")
            f.write(f"chanserv_db = {db_path}\nmemoserv_db = {data}/memoserv.db\nhistory_db = {data}/history.db\n")
            f.write("geoip_city_db = \ngeoip_asn_db = \nnetadmin_name = root\n")
            f.write(f"netadmin_password_hash = {admin_hash}\nnetadmin_hostmask = *!*@127.0.0.1\n")

        proc=start_server(binary,conf,tmp); clients=[]
        try:
            wait_listen(port,proc)
            founder=IRCClient(port); admin=IRCClient(port); clients += [founder,admin]
            register(founder,"Founder"); register(admin,"Admin")
            founder.send("NICKSERV REGISTER founderpass"); founder.expect("Nickname registered and identified.")
            admin.send("NICKSERV REGISTER adminnickpass"); admin.expect("Nickname registered and identified.")
            admin.send("OPER root adminpass"); admin.expect(" 381 Admin :You are now a Network Administrator")
            admin.send("JOIN #PersistLog"); admin.expect(" 366 Admin #PersistLog ")
            admin.send("CHANSERV REGISTER #PersistLog :logging persistence"); admin.expect("Channel registered successfully.")
            admin.send("CSSET #PersistLog FOUNDER Founder"); admin.expect("ChanServ channel updated.")
            founder.send("JOIN #PersistLog"); founder.expect(" 366 Founder #PersistLog ")
            admin.send("CHANSERV SET #PersistLog LOGGING ON"); admin.expect("Channel logging enabled.")
            founder.send("PRIVMSG #PersistLog :before-restart")

            count=wait_queue_row(db_path,"before-restart")
            assert count == 1, count
        finally:
            for c in clients:c.close()
            stop_server(proc)

        suffix=time.strftime("%d%b%Y",time.localtime()); path=os.path.join(tmp,"logs",f"PersistLog.log.{suffix}")
        assert os.path.exists(path),path
        with open(path,"r",encoding="utf-8",errors="replace") as f:
            assert "before-restart" in f.read()

        proc=start_server(binary,conf,tmp); clients=[]
        try:
            wait_listen(port,proc)
            user=IRCClient(port); admin=IRCClient(port); clients += [user,admin]
            register(user,"AfterRestart"); register(admin,"Admin2")
            admin.send("OPER root adminpass"); admin.expect(" 381 Admin2 :You are now a Network Administrator")
            user.send("JOIN #PersistLog"); user.expect(" 366 AfterRestart #PersistLog ")
            user.send("PRIVMSG #PersistLog :after-restart")
            count=wait_queue_row(db_path,"after-restart")
            assert count == 1, count
            admin.send("CHANSERV SET #PersistLog LOGGING OFF"); admin.expect("Channel logging disabled.")
            suffix=time.strftime("%d%b%Y",time.localtime()); path=os.path.join(tmp,"logs",f"PersistLog.log.{suffix}")
            assert os.path.exists(path),path
            with open(path,"r",encoding="utf-8",errors="replace") as f:text=f.read()
            assert "before-restart" in text,text
            assert "after-restart" in text,text
        finally:
            for c in clients:c.close()
            stop_server(proc)
    print("channel logging persistence integration tests passed")

if __name__=="__main__":main()
