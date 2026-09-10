#!/usr/bin/env python3
# pixserver Linux — servidor Pix (protocolo CREATE/STATUS/CANCEL) + MercadoPago.
# Porta direta do pixserver.exe (C++) para Linux, sem deps externas.
# Auth: HMAC-SHA256 challenge-response por conexão (PIX_AUTH_TOKEN). PAY removido.
import socket, threading, json, urllib.request, urllib.error, time, os, secrets, sqlite3, hmac, hashlib
import http.server, socketserver

PORT = 31736
TOKEN = os.environ.get('MP_ACCESS_TOKEN', '')
AUTH_SECRET = os.environ.get('PIX_AUTH_TOKEN', '')
ADMIN_TOKEN = os.environ.get('PIX_ADMIN_TOKEN', AUTH_SECRET)
BLACKLIST_FILE = os.environ.get('PIX_BLACKLIST', '/opt/pixserver/blacklist.txt')

def load_blacklist():
    try:
        with open(BLACKLIST_FILE) as f:
            return set(l.strip().lower() for l in f if l.strip())
    except Exception:
        return set()
PAYER_EMAIL = os.environ.get('PIX_PAYER_EMAIL', 'comprador@gmail.com')
TIMEOUT_S = int(os.environ.get('PIX_TIMEOUT_S', '180'))
DB_PATH = os.environ.get('PIX_DB', '/opt/pixserver/pix.db')

txs = {}
lock = threading.Lock()

def init_db():
    try:
        con = sqlite3.connect(DB_PATH)
        con.execute('''CREATE TABLE IF NOT EXISTS txs (
            txid TEXT PRIMARY KEY, orderId TEXT, qr TEXT, qrB64 TEXT, amount INTEGER,
            createdAt REAL, state TEXT, auth TEXT, nsu TEXT, datetime TEXT,
            pixTxid TEXT, ip TEXT, hostname TEXT)''')
        con.commit()
        for row in con.execute('SELECT * FROM txs'):
            t = {'txid': row[0], 'orderId': row[1], 'qr': row[2], 'qrB64': row[3],
                 'amount': row[4], 'createdAt': row[5], 'state': row[6], 'auth': row[7],
                 'nsu': row[8], 'datetime': row[9], 'pixTxid': row[10], 'ip': row[11], 'hostname': row[12]}
            txs[t['txid']] = t
        con.close()
    except Exception as e:
        print('init_db err:', e, flush=True)

def persist(t):
    try:
        con = sqlite3.connect(DB_PATH)
        con.execute('''INSERT OR REPLACE INTO txs VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)''',
            (t['txid'], t.get('orderId', ''), t.get('qr', ''), t.get('qrB64', ''),
             t.get('amount', 0), t.get('createdAt', 0), t['state'], t.get('auth', ''),
             t.get('nsu', ''), t.get('datetime', ''), t.get('pixTxid', ''),
             t.get('ip', ''), t.get('hostname', '')))
        con.commit(); con.close()
    except Exception as e:
        print('persist err:', e, flush=True)

def mp_create(amount_cents, ref):
    amt = "%.2f" % (amount_cents / 100.0)
    body = {
        "type": "online", "external_reference": ref, "total_amount": amt,
        "processing_mode": "automatic",
        "payer": {"email": PAYER_EMAIL},
        "transactions": {"payments": [{"amount": amt,
            "payment_method": {"id": "pix", "type": "bank_transfer"}}]}
    }
    req = urllib.request.Request('https://api.mercadopago.com/v1/orders', method='POST')
    req.add_header('Authorization', 'Bearer ' + TOKEN)
    req.add_header('Content-Type', 'application/json')
    req.add_header('X-Idempotency-Key', ref)
    try:
        r = urllib.request.urlopen(req, data=json.dumps(body).encode(), timeout=40)
        return 0, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        try: return e.code, json.loads(e.read().decode() or '{}')
        except: return e.code, {}

def mp_status(order_id):
    req = urllib.request.Request('https://api.mercadopago.com/v1/orders/' + order_id)
    req.add_header('Authorization', 'Bearer ' + TOKEN)
    try:
        r = urllib.request.urlopen(req, timeout=40)
        return json.loads(r.read().decode())
    except urllib.error.HTTPError:
        return {}

def extract_qr(resp):
    pays = resp.get('transactions', {}).get('payments') or [{}]
    pm = pays[0].get('payment_method', {})
    return pm.get('qr_code', ''), pm.get('qr_code_base64', '')

def extract_pix_txid(qr):
    i = 0
    while i + 4 <= len(qr):
        tag = qr[i:i+2]
        try: ln = int(qr[i+2:i+4])
        except: break
        if ln <= 0 or i + 4 + ln > len(qr): break
        val = qr[i+4:i+4+ln]
        if tag == '26':
            j = 0
            while j + 4 <= len(val):
                stag = val[j:j+2]
                try: sln = int(val[j+2:j+4])
                except: break
                if sln <= 0 or j + 4 + sln > len(val): break
                if stag == '01': return val[j+4:j+4+sln]
                j += 4 + sln
            return ''
        i += 4 + ln
    return ''

def fill_approval(t):
    try: v = int(t['txid'], 16)
    except: v = 0
    t['auth'] = "%06d" % (v % 1000000)
    t['nsu'] = "%012d" % (v % 1000000000000)
    t['datetime'] = time.strftime('%Y%m%d%H%M%S')

def _rc4(key: bytes, data: bytes) -> bytes:
    S = list(range(256))
    j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) & 0xFF
        S[i], S[j] = S[j], S[i]
    i = j = 0
    out = bytearray()
    for b in data:
        i = (i + 1) & 0xFF
        j = (j + S[i]) & 0xFF
        S[i], S[j] = S[j], S[i]
        out.append(b ^ S[(S[i] + S[j]) & 0xFF])
    return bytes(out)

def handle(conn):
    try:
        client_ip = conn.getpeername()[0]
    except:
        client_ip = '?'
    # ── auth HMAC-SHA256 challenge-response (falha = fecha, não processa nada) ──
    session_key = b''
    if AUTH_SECRET:
        challenge = secrets.token_hex(16)
        try:
            conn.sendall(('AUTH %s\n' % challenge).encode('utf-8'))
            conn.settimeout(10)
            # lê UMA linha (byte a byte) — recv(4096) pode engolir o 1º comando junto
            first = b''
            while not first.endswith(b'\n'):
                d = conn.recv(1)
                if not d: break
                first += d
            first = first.decode('utf-8', 'replace').strip()
            conn.settimeout(None)
        except Exception:
            conn.close(); return
        expected = hmac.new(AUTH_SECRET.encode(), challenge.encode(), hashlib.sha256).hexdigest()
        if not first.startswith('AUTH ') or not hmac.compare_digest(first[5:].strip(), expected):
            try: conn.sendall(b'ERR 6 unauthorized\n')
            except Exception: pass
            conn.close(); return
        session_key = expected.encode()  # RC4 do payload = HMAC(secret, challenge)
    else:
        print('AVISO: PIX_AUTH_TOKEN nao setado — auth DESABILITADO (producao exige)', flush=True)
    buf = b''
    try:
        while True:
            d = conn.recv(4096)
            if not d: break
            buf += d
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                line = line.decode('utf-8', 'replace').strip()
                if not line: continue
                # fase 2 (pós-handshake): comando cifrado "E <hex>"
                if line.startswith('E ') and session_key:
                    try: line = _rc4(session_key, bytes.fromhex(line[2:])).decode('utf-8', 'replace').strip()
                    except Exception: line = ''
                    if not line: continue
                parts = line.split()
                cmd = parts[0].upper()
                resp = ''
                if cmd == 'CREATE' and len(parts) >= 4:
                    try: cents = int(parts[2])
                    except: cents = 0
                    hostname = parts[4] if len(parts) >= 5 else ''
                    # blacklist (recarregada a cada CREATE — bloqueia IP/hostname sem restart)
                    bl = load_blacklist()
                    if client_ip in bl or hostname.strip().lower() in bl:
                        resp = "ERR 7 blacklisted\n"
                    else:
                        txid = secrets.token_hex(8).upper()  # 16 hex imprevisível
                        ref = "VENDA-%s-%d" % (txid, int(time.time() * 1000) % 1000000000)
                        code, r = mp_create(cents, ref)
                        if code == 0 and r.get('id'):
                            qr, qrB64 = extract_qr(r)
                            with lock:
                                txs[txid] = {'txid': txid, 'orderId': r.get('id', ''), 'qr': qr,
                                             'qrB64': qrB64, 'amount': cents, 'createdAt': time.time(),
                                             'state': 'PEN', 'auth': '', 'nsu': '', 'datetime': time.strftime('%Y%m%d%H%M%S'),
                                             'pixTxid': extract_pix_txid(qr),
                                             'ip': client_ip, 'hostname': hostname}
                                persist(txs[txid])
                            resp = "OK|%s|%s|%s\n" % (txid, qr, qrB64)
                        else:
                            resp = "ERR 5 create failed (mp=%d)\n" % code
                elif cmd == 'STATUS' and len(parts) >= 2:
                    txid = parts[1]
                    with lock:
                        t = txs.get(txid)
                        if not t:
                            resp = "ERR 3 not found\n"
                        else:
                            if t['state'] == 'PEN':
                                r = mp_status(t['orderId'])
                                st = r.get('status', '')
                                if st in ('approved', 'processed'):
                                    t['state'] = 'APPROVED'; fill_approval(t); persist(t)
                                elif st in ('rejected', 'cancelled', 'failed'):
                                    t['state'] = 'DENIED'; persist(t)
                                elif time.time() - t['createdAt'] > TIMEOUT_S:
                                    t['state'] = 'TIMEOUT'; persist(t)
                            if t['state'] == 'PEN':
                                resp = "PEN\n"
                            elif t['state'] == 'APPROVED':
                                resp = "APPROVED %s %s %s|%s|%s\n" % (t['auth'], t['nsu'], t['datetime'], t['orderId'], t['pixTxid'])
                            elif t['state'] == 'CANCELED':
                                resp = "CANCELED\n"
                            elif t['state'] == 'TIMEOUT':
                                resp = "TIMEOUT\n"
                            elif t['state'] == 'DENIED':
                                resp = "DENIED denied\n"
                elif cmd == 'CANCEL' and len(parts) >= 2:
                    txid = parts[1]
                    with lock:
                        t = txs.get(txid)
                        if not t: resp = "ERR 3 not found\n"
                        elif t['state'] != 'PEN': resp = "ERR 4 already finalized\n"
                        else: t['state'] = 'CANCELED'; persist(t); resp = "OK\n"
                else:
                    resp = "ERR 1 unknown command\n"
                if resp:
                    resp = resp.rstrip('\n')
                    if session_key:
                        conn.sendall(('E ' + _rc4(session_key, resp.encode('utf-8')).hex() + '\n').encode('utf-8'))
                    else:
                        conn.sendall((resp + '\n').encode('utf-8'))
    except Exception as e:
        print('handle err:', e, flush=True)
    finally:
        conn.close()

def start_monitor():
    class H(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.startswith('/stats'):
                with lock:
                    txs_out = []
                    total_aprovado = 0.0; total_pendente = 0.0
                    qtd = {'PEN': 0, 'APPROVED': 0, 'CANCELED': 0, 'TIMEOUT': 0, 'DENIED': 0}
                    for t in txs.values():
                        amt = round(t.get('amount', 0) / 100.0, 2)
                        st = t['state']
                        qtd[st] = qtd.get(st, 0) + 1
                        txs_out.append({'txid': t['txid'], 'orderId': t.get('orderId', ''),
                                        'amount': amt, 'state': st, 'datetime': t.get('datetime') or '',
                                        'ip': t.get('ip', ''), 'hostname': t.get('hostname', ''),
                                        'qr': t.get('qr', ''), 'qrB64': t.get('qrB64', '')})
                        if st == 'APPROVED': total_aprovado += amt
                        if st == 'PEN': total_pendente += amt
                    data = json.dumps({'txs': txs_out,
                                       'total_aprovado': round(total_aprovado, 2),
                                       'total_pendente': round(total_pendente, 2), 'qtd': qtd})
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Cache-Control', 'no-cache')
                self.end_headers()
                self.wfile.write(data.encode())
            elif self.path in ('/', '/index.html'):
                try:
                    with open('/opt/pixserver/dashboard.html', 'rb') as f:
                        html = f.read()
                except:
                    html = b'dashboard nao encontrado'
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
                self.end_headers()
                self.wfile.write(html)
            elif self.path.startswith('/blacklist'):
                import html as html_mod, urllib.parse as up
                tok = up.parse_qs(up.urlparse(self.path).query).get('t', [''])[0]
                if not ADMIN_TOKEN or tok != ADMIN_TOKEN:
                    self.send_response(403)
                    self.send_header('Content-Type', 'text/plain; charset=utf-8')
                    self.end_headers()
                    self.wfile.write(b'acesso negado (token)')
                    return
                entries = sorted(load_blacklist())
                rows = ''.join('<li>%s <button data-entry="%s">remover</button></li>' % (html_mod.escape(e), html_mod.escape(e)) for e in entries)
                html = ('''<!doctype html><meta charset="utf-8"><title>Blacklist</title>
                <h2>Blacklist (IP/hostname)</h2>
                <ul>%s</ul>
                <form method="POST" action="/blacklist?t=%s">
                  <input name="add" placeholder="IP ou hostname">
                  <button>Adicionar</button>
                </form>
                <script>
                function rem(e){fetch('/blacklist?t=%s',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'remove='+encodeURIComponent(e)}).then(()=>location.reload());}
                document.querySelectorAll('button[data-entry]').forEach(function(b){b.addEventListener('click',function(){rem(b.getAttribute('data-entry'));});});
                </script>''' % (rows, tok, tok)).encode('utf-8')
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
                self.end_headers()
                self.wfile.write(html)
            else:
                self.send_response(404); self.end_headers()
        def do_POST(self):
            if self.path.startswith('/blacklist'):
                import urllib.parse as up
                tok = up.parse_qs(up.urlparse(self.path).query).get('t', [''])[0]
                if not ADMIN_TOKEN or tok != ADMIN_TOKEN:
                    self.send_response(403); self.end_headers(); return
                length = int(self.headers.get('Content-Length', 0))
                body = self.rfile.read(length).decode('utf-8', 'replace')
                q = up.parse_qs(body)
                add = q.get('add', [''])[0].strip().lower()
                rem = q.get('remove', [''])[0].strip().lower()
                entries = load_blacklist()
                if add: entries.add(add)
                if rem: entries.discard(rem)
                try:
                    with open(BLACKLIST_FILE, 'w') as f:
                        for e in sorted(entries):
                            f.write(e + '\n')
                except Exception as e:
                    print('blacklist write err:', e, flush=True)
                self.send_response(302)
                self.send_header('Location', '/blacklist?t=' + tok)
                self.end_headers()
            else:
                self.send_response(404); self.end_headers()
        def log_message(self, *a): pass
    socketserver.ThreadingTCPServer.allow_reuse_address = True
    srv = socketserver.ThreadingTCPServer(('0.0.0.0', 8080), H)
    srv.serve_forever()

def main():
    if not TOKEN:
        print('ERRO: MP_ACCESS_TOKEN nao setado', flush=True); return 1
    init_db()
    print('db carregado: %d transacoes' % len(txs), flush=True)
    threading.Thread(target=start_monitor, daemon=True).start()
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('0.0.0.0', PORT)); s.listen(50)
    print('pixserver listening 0.0.0.0:%d (monitor 8080)' % PORT, flush=True)
    while True:
        c, _ = s.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()

if __name__ == '__main__':
    main()
