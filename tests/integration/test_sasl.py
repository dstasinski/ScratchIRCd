#!/usr/bin/env python3
"""End-to-end IRCv3 CAP + SASL PLAIN authentication coverage."""
import base64, os, socket, subprocess, sys, tempfile, time

class IRCClient:
    def __init__(self, port):
        self.sock=socket.create_connection(("127.0.0.1",port),timeout=3.0); self.sock.settimeout(0.25); self.buf=b""
    def send(self,line): self.sock.sendall((line+"\r\n").encode())
    def expect(self,needle,duration=5.0):
        end=time.monotonic()+duration; got=[]
        while time.monotonic()<end:
            while b"\n" in self.buf:
                raw,self.buf=self.buf.split(b"\n",1); line=raw.rstrip(b"\r").decode(errors="replace"); got.append(line)
                if needle in line: return got
            try:
                data=self.sock.recv(4096)
                if not data: break
                self.buf+=data
            except socket.timeout: pass
        raise AssertionError(f"expected {needle!r}; got {got!r}")
    def read_for(self,duration=0.4):
        end=time.monotonic()+duration; got=[]
        while time.monotonic()<end:
            while b"\n" in self.buf:
                raw,self.buf=self.buf.split(b"\n",1); got.append(raw.rstrip(b"\r").decode(errors="replace"))
            try:
                data=self.sock.recv(4096)
                if not data: break
                self.buf+=data
            except socket.timeout: pass
        while b"\n" in self.buf:
            raw,self.buf=self.buf.split(b"\n",1); got.append(raw.rstrip(b"\r").decode(errors="replace"))
        return got
    def close(self):
        try:self.sock.close()
        except OSError:pass

def free_port():
    s=socket.socket(); s.bind(("127.0.0.1",0)); p=s.getsockname()[1]; s.close(); return p

def wait_listen(port,proc):
    end=time.monotonic()+5
    while time.monotonic()<end:
        if proc.poll() is not None: raise RuntimeError(proc.stderr.read())
        try: s=socket.create_connection(("127.0.0.1",port),timeout=.1); s.close(); return
        except OSError: time.sleep(.05)
    raise RuntimeError("listener did not start")

def register(c,nick):
    c.send(f"NICK {nick}"); c.send(f"USER {nick} 0 * :{nick}"); c.expect(f" 001 {nick} ")

def plain(account,password):
    return base64.b64encode(("\0"+account+"\0"+password).encode()).decode()

def encoded(raw):
    return base64.b64encode(raw).decode()

def begin_sasl(client,nick):
    client.send("CAP LS 302"); client.expect(" CAP * LS :")
    client.send("CAP REQ :sasl"); client.expect(" CAP * ACK :sasl")
    client.send(f"NICK {nick}"); client.send(f"USER sasltest 0 * :{nick}")
    client.send("AUTHENTICATE PLAIN"); client.expect("AUTHENTICATE +")

def finish_unauthenticated(client,nick):
    client.send("CAP END"); client.expect(f" 001 {nick} ")
    client.send(f"WHOIS {nick}")
    whois=client.expect(f" 318 {nick} {nick} ")
    assert not any("is logged in as" in line for line in whois),whois

def expect_sasl_rejection(port,clients,nick,payload,numeric="904"):
    client=IRCClient(port); clients.append(client)
    begin_sasl(client,nick)
    client.send("AUTHENTICATE "+payload); client.expect(f" {numeric} {nick} ")
    # A rejected exchange must not authenticate the connection or prevent
    # ordinary unauthenticated registration after CAP negotiation ends.
    finish_unauthenticated(client,nick)

def main():
    binary=os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="scratchircd-sasl-") as td:
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
            seed=IRCClient(port); clients.append(seed); register(seed,"Alice")
            seed.send("NICKSERV REGISTER secretpass"); seed.expect("Nickname registered and identified.")
            seed.send("QUIT :seed done"); seed.close(); clients.remove(seed)

            long_password="p"*420
            chunk_seed=IRCClient(port); clients.append(chunk_seed); register(chunk_seed,"Chunked")
            chunk_seed.send("NICKSERV REGISTER "+long_password)
            chunk_seed.expect("Nickname registered and identified.")
            chunk_seed.send("QUIT :seed done"); chunk_seed.close(); clients.remove(chunk_seed)

            c=IRCClient(port); clients.append(c)
            c.send("CAP LS 302"); ls=c.expect(" CAP * LS :")
            assert any("sasl" in line for line in ls),ls
            c.send("CAP REQ :sasl"); c.expect(" CAP * ACK :sasl")
            c.send("NICK Traveler"); c.send("USER traveler 0 * :Traveler")
            c.send("AUTHENTICATE PLAIN"); c.expect("AUTHENTICATE +")
            c.send("AUTHENTICATE "+plain("Alice","secretpass")); c.expect(" 903 Traveler ")
            c.send("AUTHENTICATE PLAIN"); c.expect(" 907 Traveler ")
            # Registration remains held until CAP END.
            c.send("CAP END"); c.expect(" 001 Traveler ")
            c.send("MODE Traveler"); modes=c.expect(" 221 Traveler ")
            assert any("r" in line.rsplit(" ",1)[-1] for line in modes if " 221 Traveler " in line),modes
            c.send("WHOIS Traveler"); whois=c.expect(" 318 Traveler Traveler ")
            assert any("is logged in as Alice" in line for line in whois),whois

            bad=IRCClient(port); clients.append(bad)
            bad.send("CAP LS 302"); ls=bad.expect(" CAP * LS :")
            assert any("sasl" in line for line in ls),ls
            bad.send("CAP REQ :sasl"); bad.expect(" CAP * ACK :sasl")
            bad.send("NICK Bad"); bad.send("USER bad 0 * :Bad")
            bad.send("AUTHENTICATE PLAIN"); bad.expect("AUTHENTICATE +")
            bad.send("AUTHENTICATE "+plain("Alice","wrong")); bad.expect(" 904 Bad ")
            bad.send("CAP END"); bad.expect(" 001 Bad ")

            chunked=IRCClient(port); clients.append(chunked); begin_sasl(chunked,"Framed")
            chunked_payload=plain("Chunked",long_password)
            assert 400<len(chunked_payload)<=800,len(chunked_payload)
            chunked.send("AUTHENTICATE "+chunked_payload[:400])
            interim=chunked.read_for()
            assert not any(f" 90{n} Framed " in line for n in range(3,8) for line in interim),interim
            chunked.send("AUTHENTICATE "+chunked_payload[400:]); chunked.expect(" 903 Framed ")
            chunked.send("CAP END"); chunked.expect(" 001 Framed ")

            exact=IRCClient(port); clients.append(exact); begin_sasl(exact,"ExactFrame")
            exact.send("AUTHENTICATE "+"A"*400)
            interim=exact.read_for()
            assert not any(" 904 ExactFrame " in line for line in interim),interim
            exact.send("AUTHENTICATE +"); exact.expect(" 904 ExactFrame ")
            finish_unauthenticated(exact,"ExactFrame")

            cancelled=IRCClient(port); clients.append(cancelled); begin_sasl(cancelled,"Cancelled")
            cancelled.send("AUTHENTICATE "+"A"*400)
            cancelled.send("AUTHENTICATE *"); cancelled.expect(" 906 Cancelled ")
            cancelled.send("AUTHENTICATE PLAIN"); cancelled.expect("AUTHENTICATE +")
            cancelled.send("AUTHENTICATE "+plain("Alice","secretpass")); cancelled.expect(" 903 Cancelled ")
            cancelled.send("CAP END"); cancelled.expect(" 001 Cancelled ")

            malformed=[
                ("NoSeparators",encoded(b"Alice")),
                ("OneSeparator",encoded(b"\0Alice")),
                ("EmptyAuthcid",encoded(b"\0\0secretpass")),
                ("EmptyPassword",encoded(b"\0Alice\0")),
                ("ExtraSeparator",encoded(b"\0Alice\0secretpass\0ignored")),
                ("WrongAuthzid",encoded(b"Mallory\0Alice\0secretpass")),
                ("InvalidBase64","!!!!"),
            ]
            for nick,payload in malformed:
                expect_sasl_rejection(port,clients,nick,payload)
            expect_sasl_rejection(port,clients,"TooLong","A"*404,"905")

            aggregate=IRCClient(port); clients.append(aggregate); begin_sasl(aggregate,"Aggregate")
            aggregate.send("AUTHENTICATE "+"A"*400)
            aggregate.send("AUTHENTICATE "+"A"*400)
            aggregate.send("AUTHENTICATE AAAA"); aggregate.expect(" 905 Aggregate ")
            finish_unauthenticated(aggregate,"Aggregate")
        finally:
            for c in clients:c.close()
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(timeout=3)
                except subprocess.TimeoutExpired: proc.kill(); proc.wait(timeout=3)
if __name__=="__main__": main()
