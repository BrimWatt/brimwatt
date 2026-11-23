"""Stand-in for the board: serves the current dashboard.html with the demo script's sample data, and fakes /api/car/* statefully (a paired Model Y,
Tesla mode, Bluetooth jobs). Only for looking at the UI and for make_neighbor_demo.py; no board or car is involved.
Run: python3 docs/demo-src/car_standin.py   (port 8099; /fake/set?paired=1&modeOn=1&through=1&reading=1 sets its state)"""
import json, re, time, urllib.parse
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[2]
DASH = str(ROOT / 'src/main/resources/dashboard.html')
DEMO = (ROOT / 'docs/demo-src/make_demo.py').read_text()
def grab(name): return re.search(name + r' = r"""(.*?)"""', DEMO, re.S).group(1)
SHIM, AFTER = grab('SHIM'), grab('AFTER')
BASE = dict(mode="ECO", paused=False, fastAmps=24, daylightOpen=True, solar=7800, grid=-150, chargerW=7200, home=400, status="CHARGING", amps=30, goalKwh=0, goalDone=0, charging=True, carToday=9.6, carSolarToday=7.9, miles=False)
WRAP = '''<script>
(function () {
  const shimFetch = window.fetch, real = window.__realFetch;
  window.fetch = (url, opts) => {
    const p = String(url).split('?')[0];
    if (p.startsWith('/api/car/')) return real(url, opts);
    if (p === '/api/telemetry') return shimFetch(url).then(async r => { const d = await r.json(); const c = await (await real('/fake/car')).json(); const meta = c._meta || {}; delete c._meta; if (d.controller) d.controller = Object.assign({}, d.controller, { failSafe: meta.failSafe }); delete meta.failSafe; return { ok: true, status: 200, json: async () => Object.assign({}, d, { car: c }, meta), text: async () => '' }; });
    return shimFetch(url, opts);
  };
})();
</script>'''
def page(q):
    cfg = dict(BASE); cfg['settings'] = q.get('settings') == '1'; cfg['open'] = q.get('open', '').split(',') if q.get('open') else []
    if q.get('scrollTo'): cfg['scrollTo'] = q['scrollTo']
    if q.get('scrollY'): cfg['scrollY'] = int(q['scrollY'])
    c = json.dumps(cfg); html = open(DASH).read()
    inject = '<script>window.__realFetch = window.fetch.bind(window);</script>' + SHIM.replace('__CONFIG__', c) + WRAP
    html = html.replace('<script>\nconst el', inject + '<script>\nconst el', 1)
    card = ('<script>setTimeout(() => { const t = document.querySelector(%s); const c = document.querySelector("#settingsbox .card"); if (t && c) c.scrollTop += t.getBoundingClientRect().top - c.getBoundingClientRect().top - 70; }, 2300);</script>' % json.dumps(q['card'])) if q.get('card') else ''
    return html.replace('</body>', AFTER.replace('__CONFIG__', c) + card + '</body>')
S = dict(paired=False, watch=True, job='', jobStart=0, jobKind='', result=None, resultAt=0, reading=None, readingAt=0, restore=0, amps=18, charging=5, sleep=1, supported=True, through=False, onReturn='keep', ctlState='', out=0, allowWake=False, modeOn=False, clock=True, meter='ok', fs=False)

def now(): return time.time()

def tick():
    if not S['job']: return
    t = now() - S['jobStart']
    if S['job'] == 'pair' and t > 11:
        S.update(job='', paired=True, result=dict(job='pair', ok=True, lines=['The car accepted our key.']), resultAt=now())
        read()
    elif S['job'] in ('status', 'charge') and t > 3:
        kind = S['job']; S['job'] = ''
        S['result'] = dict(job=kind, ok=True, lines=S.pop('pendingLines', ['Car is awake, unlocked.'])); S['resultAt'] = now()
        read()

def read():
    S['reading'] = dict(sleep=S['sleep'], lock=0, charging='charging' if S['charging'] == 5 else 'stopped', chargingCode=S['charging'], battery=46, limit=80,
                        amps=S['amps'] if S['charging'] == 5 else 0, requestAmps=S['amps'], maxAmps=48, volts=239, kw=4)
    S['readingAt'] = now()

def car():
    tick()
    if not S['supported']: return dict(supported=False, _meta=dict(meterLinked=S['meter']!='none', enphaseOk=S['meter']=='ok', failSafe=S['fs']))
    c = dict(supported=True, paired=S['paired'], vin='5YJ**********4667' if S['paired'] else '', key='9b4593c7', watch=S['watch'], busy=bool(S['job']), job=S['job'], restoreAmps=S['restore'])
    if S['job'] == 'pair':
        t = now() - S['jobStart']
        c['pair'] = dict(state='finding' if t < 4 else 'waiting', text='' if t < 4 else 'The car is waiting for your key card: tap it on the centre console, then confirm on the screen if asked.', secondsLeft=max(0, int(120 - t)))
    c['_meta'] = dict(meterLinked=S['meter']!='none', enphaseOk=S['meter']=='ok', failSafe=S['fs'])
    c['mode'] = dict(enabled=S['modeOn'], restartPending=False, clockSet=S['clock'], hotspot='wattsync-tesla', hotspotUp=S['modeOn'], hotspotClients=1)
    st = S['ctlState'] or ("WattSync is controlling the car's charging." if S['through'] else 'The charger is in command.')
    c['control'] = dict(through=S['through'], state=st, unreachableSeconds=S['out'], problem='', onReturn=S['onReturn'], allowWake=S['allowWake'], lastWakeAgeSeconds=-1)
    if S['result']: c['result'] = dict(S['result'], ageSeconds=int(now() - S['resultAt']))
    if S['reading'] and S['paired']: c['reading'] = dict(S['reading'], ageSeconds=int(now() - S['readingAt']))
    return c

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body, ctype='text/plain'):
        b = body if isinstance(body, bytes) else body.encode()
        self.send_response(code); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(b))); self.send_header('Cache-Control', 'no-store'); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        if u.path == '/': return self.send(200, page(dict(urllib.parse.parse_qsl(u.query))), 'text/html; charset=utf-8')
        if u.path == '/frame':   # a 402 px wide frame around the page, same origin (headless Chrome will not make a window that narrow)
            q = dict(urllib.parse.parse_qsl(u.query))
            return self.send(200, '<!doctype html><body style="margin:0;background:#fff"><iframe src="/?%s" width="402" height="%s" style="border:0;display:block"></iframe></body>' % (q.get('q', '').replace('"', ''), int(q.get('h', '800'))), 'text/html; charset=utf-8')
        if u.path == '/fake/car': return self.send(200, json.dumps(car()), 'application/json')
        if u.path == '/api/car/hotspot': return self.send(200 if S['modeOn'] else 400, json.dumps(dict(ssid='wattsync-tesla', password='demo-only-password')) if S['modeOn'] else 'Tesla mode is off.', 'application/json')
        if u.path == '/favicon.ico': return self.send(404, '')
        if u.path == '/fake/set':
            q = dict(urllib.parse.parse_qsl(u.query))
            for k, v in q.items():
                if k in ('paired', 'watch', 'supported', 'modeOn', 'clock'): S[k] = v == '1'
                elif k in ('amps', 'charging', 'sleep', 'restore', 'out'): S[k] = int(v)
                elif k in ('through',): S[k] = v == '1'
                elif k in ('ctlState', 'onReturn', 'meter'): S[k] = v
                elif k == 'fs': S['fs'] = v == '1'
            if 'reading' in q: read() if q['reading'] == '1' else S.update(reading=None)
            if 'old' in q and S['reading']: S['readingAt'] = now() - int(q['old'])
            if 'fail' in q: S.update(result=dict(job='status', ok=False, lines=['The car was not heard. Move the board closer (try `tesla scan`) and try again.']), resultAt=now())
            return self.send(200, 'ok')
        return self.send(404, 'not found')
    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        n = int(self.headers.get('Content-Length') or 0); body = self.rfile.read(n).decode() if n else ''
        if u.path == '/api/clock': S['clock'] = True; return self.send(200, 'Clock set')
        if u.path.startswith('/api/car/'):
            q = dict(urllib.parse.parse_qsl(u.query)); q.update(dict(urllib.parse.parse_qsl(body)))
            tick()
            if self.headers.get('X-WattSync') != '1': return self.send(403, 'Blocked')
            if not S['supported']: return self.send(400, 'Car control needs the ESP32 board.')
            p = u.path
            if p == '/api/car/mode':
                S['modeOn'] = q.get('enabled') == 'true'; S['through'] = S['modeOn'] and S['paired']; return self.send(200, 'ok')
            if p == '/api/car/pair':
                import re
                if not re.fullmatch(r'[A-HJ-NPR-Z0-9]{17}', q.get('vin', '')): return self.send(400, 'That is not a VIN (17 letters and digits, no I, O or Q).')
                if S['job']: return self.send(409, 'The car is busy with another job.')
                S.update(job='pair', jobStart=now()); return self.send(200, 'Pairing started')
            if p == '/api/car/through':
                if q.get('mode') == 'car':
                    if not S['paired']: return self.send(400, 'No car is paired yet.')
                    if q.get('confirm') != 'yes': return self.send(400, 'Switching to the car needs confirm=yes.')
                    S['through'] = True; S['watch'] = S['watch']
                elif q.get('mode') == 'charger': S['through'] = False; S['ctlState'] = ''; S['out'] = 0
                else: return self.send(400, 'mode must be charger or car')
                return self.send(200, 'ok')
            if p == '/api/car/policy':
                if q.get('onReturn') not in ('keep', 'tell', 'stop'): return self.send(400, 'onReturn must be keep, tell or stop')
                S['onReturn'] = q['onReturn']; return self.send(200, 'Saved')
            if p == '/api/car/allowwake': S['allowWake'] = q.get('enabled') == 'true'; return self.send(200, 'Saved')
            if p == '/api/car/wake':
                if S['job']: return self.send(409, 'The car is busy with another job.')
                S['pendingLines'] = ['The car is awake.']; S.update(job='charge', jobStart=now()); return self.send(200, 'Waking the car')
            if p == '/api/car/cancel': S.update(job='', result=dict(job='pair', ok=False, lines=['Stopped.']), resultAt=now()); return self.send(200, 'Stopped')
            if p == '/api/car/forget': S.update(paired=False, reading=None, result=None, restore=0); return self.send(200, 'Car forgotten')
            if p == '/api/car/watch': S['watch'] = q.get('enabled') == 'true'; return self.send(200, 'ok')
            if p == '/api/car/refresh':
                S.update(job='status', jobStart=now()); return self.send(200, 'Reading the car')
            if p == '/api/car/test':
                if S['job']: return self.send(409, 'The car is busy with another job.')
                a = q.get('action')
                if a == 'start': S['charging'] = 5; S['pendingLines'] = ['Charging started.']
                elif a == 'stop': S['charging'] = 7; S['pendingLines'] = ['Charging stopped.']
                else:
                    n2 = int(q.get('amps', '0'))
                    if not 6 <= n2 <= 40: return self.send(400, 'amps must be a whole number from 6 to 40.')
                    if not S['restore']: S['restore'] = S['amps']
                    elif n2 == S['restore']: S['restore'] = 0
                    S['amps'] = n2; S['pendingLines'] = ['Charging current set to %d A.' % n2]
                S.update(job='charge', jobStart=now()); return self.send(200, 'ok')
            return self.send(404, 'Not found')
        return self.send(200, 'ok')

ThreadingHTTPServer(('127.0.0.1', 8099), H).serve_forever()
