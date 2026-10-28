// Dashboard checks that need no browser: run with  node --test tests/dashboard/
//
// The page is one HTML file, so these tests load it, run its script against a stand-in for the page (every element is a
// stub that accepts any call), and then check the pure wording and number-formatting functions, plus the wiring that
// breaks silently: every id the script asks for exists, every button handler exists, every help card has its text.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const html = fs.readFileSync(path.join(here, '..', '..', 'src', 'main', 'resources', 'dashboard.html'), 'utf8');
const script = html.match(/<script>([\s\S]*)<\/script>\s*<\/body>/)[1];

/** An object that accepts any property, call or assignment and keeps returning itself. */
function stub() {
    const handler = {
        get: (target, key) => {
            if (key === Symbol.toPrimitive) return () => '';
            if (key === 'then') return undefined;               // so awaiting it does not hang
            if (key === 'length') return 0;
            if (key === 'classList') return { add() {}, remove() {}, toggle() {}, contains: () => false };
            if (key === 'style') return {};
            if (key === 'dataset') return {};
            if (key === 'querySelector') return () => stub();
            if (key === 'querySelectorAll') return () => [];
            if (key === 'closest') return () => stub();
            return proxy;
        },
        apply: () => proxy,
        set: () => true,
    };
    const proxy = new Proxy(function () {}, handler);
    return proxy;
}

function load(search = '') {
    const document = {
        getElementById: () => stub(), querySelector: () => stub(), querySelectorAll: () => [], createElement: () => stub(),
        createElementNS: () => stub(), addEventListener() {}, body: stub(), documentElement: stub(), activeElement: stub(),
        hidden: false,
    };
    const context = {
        document, console, Math, Date, JSON, Number, String, Array, Object, Map, Set, Promise, Intl, Error, DOMException: Error,
        parseInt, parseFloat, isNaN, encodeURIComponent,
        window: { addEventListener() {}, navigator: {}, matchMedia: () => ({ matches: false }), scrollY: 0, scrollTo() {} },
        navigator: { standalone: false }, location: { search, reload() {} }, matchMedia: () => ({ matches: false }),
        fetch: () => Promise.reject(new Error('offline in tests')),
        AbortController: class { constructor() { this.signal = {}; } abort() {} },
        ResizeObserver: class { observe() {} },
        setInterval: () => 0, clearInterval() {}, setTimeout: () => 0, clearTimeout() {},
        sessionStorage: {}, localStorage: {}, devicePixelRatio: 1, innerWidth: 390, innerHeight: 800,
    };
    context.window.document = document;
    vm.createContext(context);
    vm.runInContext(script, context);
    return context;
}

const page = load();
const read = expr => vm.runInContext(expr, page);

test('the page script loads without throwing', () => assert.ok(typeof page.refresh === 'function'));

test('elapsed times are short and round the way we agreed', () => {
    assert.equal(page.fmtAge(-1), 'never');
    assert.equal(page.fmtAge(2), 'just now');
    assert.equal(page.fmtAge(30), '30s ago');
    assert.equal(page.fmtAge(45 * 60), '45m ago');
    assert.equal(page.fmtAge(60 * 60), '~1h ago');
    assert.equal(page.fmtAge(90 * 60), '~1h ago');            // half an hour past the hour or less rounds down
    assert.equal(page.fmtAge(91 * 60), '~2h ago');            // more than that rounds up
    assert.equal(page.fmtAge(286 * 60), '~5h ago');
    assert.equal(page.fmtAge(36 * 3600), '~1d ago');
    assert.equal(page.fmtAge(3 * 86400), '~3d ago');
});

test('power is shown in W below a kilowatt and kW above', () => {
    assert.equal(page.fmtPower(0), '0 W');
    assert.equal(page.fmtPower(322), '322 W');
    assert.equal(page.fmtPower(2160), '2.16 kW');
});

const base = () => ({
    currentMode: 'ECO', paused: false, gridAllowanceWatts: 600, chargerConnected: true, enphaseOk: true,
    daylight: { enabled: true, open: true }, power: { voltage: 240, solarWatts: 2420, gridWatts: 132, chargerWatts: 2160, homeWatts: 388 },
    charger: { status: 'CHARGING', amps: 9, limitAmps: 9 },
    controller: { hasReading: true, reason: 'no change', gridAllowanceWatts: 600, failSafe: false, readingAgeSeconds: 5, pollSeconds: 60 },
});
const why = mutate => { const d = base(); mutate(d); return page.whyText(d); };

test('the headline explains why it is not charging, in plain words', () => {
    assert.equal(why(d => { d.paused = true; }), 'Paused by you');
    assert.equal(why(d => { d.charger.status = 'NONE'; }), 'Charger is offline');
    assert.equal(why(d => { d.charger.status = 'AVAILABLE'; }), 'No car plugged in');
    assert.equal(why(d => { d.charger.status = 'FINISHING'; }), 'Charge finished');
    assert.equal(why(d => { d.charger.status = 'FINISHING'; d.currentMode = 'FAST'; }), 'Charge finished');
    assert.equal(why(d => { d.charger.status = 'SUSPENDED_EV'; }), 'Car has stopped taking power');
    assert.equal(why(d => { d.charger.status = 'PREPARING'; d.daylight.open = false; }), 'Outside solar hours');
    assert.equal(why(d => { d.charger.status = 'PREPARING'; d.controller.failSafe = true; }), 'Lost contact with the solar meter: charging stopped');
    assert.equal(why(d => { d.charger.status = 'PREPARING'; d.controller.hasReading = false; }), 'Getting the first solar reading…');
});

test('Eco blames the house when the sun is there but the house uses it; Max solar never mentions the house', () => {
    const eco = d => { d.charger.status = 'PREPARING'; d.controller.reason = 'waiting for surplus'; d.power.solarWatts = 3000; d.power.homeWatts = 3500; };
    assert.equal(why(eco), 'Home is using the solar');
    const max = d => { eco(d); d.currentMode = 'MAX_SOLAR'; };
    assert.equal(why(max), 'Enough solar, checking it lasts');
    assert.equal(why(d => { max(d); d.power.solarWatts = 300; }), 'Not enough solar yet');
    assert.equal(why(d => { max(d); d.power.solarWatts = 300; d.controller.reason = 'stop: surplus below the minimum, would import more than the allowance'; }), 'Stopped: not enough solar');
});

test('charging amps are shown with the power they mean', () => {
    assert.equal(page.ampsWithKw(9, base()), '9 A (about 2.2 kW)');
    assert.equal(page.ampsWithKw(40, base()), '40 A (about 9.6 kW)');
});

test('every ending reason from the wallbox has plain words', () => {
    const reasons = read('END_REASON');
    for (const code of ['Remote', 'EVDisconnected', 'Local', 'EmergencyStop', 'PowerLoss', 'Reboot', 'SoftReset', 'HardReset', 'DeAuthorized', 'UnlockCommand']) {
        assert.ok(reasons[code] && /^[a-z]/.test(reasons[code]), code + ' needs a sentence');
    }
});

test('the modes are named for people, and the old name is gone from the page', () => {
    assert.deepEqual({ ...read('MODE_NAME') }, { ECO: 'Eco', MAX_SOLAR: 'Max solar', FAST: 'Custom' });
    assert.ok(!/BALANCED/.test(html), 'the internal name is MAX_SOLAR now');
});

// ---------------------------------------------------------------- wiring that breaks silently

const ids = new Set([...html.matchAll(/\bid="([^"]+)"/g)].map(m => m[1]));

test('every id the script looks up by name exists in the page', () => {
    const wanted = [...script.matchAll(/\bel\('([^']+)'\)/g)].map(m => m[1]);
    const missing = [...new Set(wanted)].filter(id => !ids.has(id));
    assert.deepEqual(missing, [], 'ids the script asks for but the page does not have');
});

test('every button handler written in the page is a function in the script', () => {
    const called = [...html.matchAll(/\bon(?:click|change|input)="([A-Za-z_]\w*)\(/g)].map(m => m[1]);
    const missing = [...new Set(called)].filter(name => typeof page[name] !== 'function');
    assert.deepEqual(missing, []);
});

test('every help button has a help card and every help card has text', () => {
    const tips = read('TIPS');
    const used = [...html.matchAll(/data-tip="([^"]+)"/g)].map(m => m[1]);
    for (const key of new Set(used)) assert.ok(tips[key], 'no help card for "' + key + '"');
    for (const [key, [title, lines]] of Object.entries(tips)) {
        assert.ok(title && lines.length >= 1, key + ' needs a title and text');
        for (const line of lines) assert.ok(line.length < 260, key + ' has a line that is too long to read at a glance');
    }
    for (const key of Object.keys(tips)) assert.ok(used.includes(key), 'help card "' + key + '" is never used');
});

test('every control has a name a screen reader can say', () => {
    const buttons = [...html.matchAll(/<button\b([^>]*)>([\s\S]*?)<\/button>/g)];
    for (const [, attrs, inner] of buttons) {
        const label = /aria-label="[^"]+"/.test(attrs) || inner.replace(/<[^>]+>/g, '').trim().length > 0;
        assert.ok(label, 'a button without a name: ' + attrs.slice(0, 60));
    }
});

test('changing requests carry the safe header', () => {
    assert.match(script, /headers:\s*\{\s*'X-WattSync':\s*'1'\s*\}/);
});

test('pull to refresh stays off in a normal tab and switches on for the Home Screen app', () => {
    const off = load('');
    assert.equal(off.navigator.standalone, false);
    const sample = fs.readFileSync(path.join(here, '..', '..', 'src', 'main', 'resources', 'dashboard.html'), 'utf8');
    assert.match(sample, /standalone = window\.navigator\.standalone === true/);
});

test('the offline notice does not reuse the round help-button class', () => {
    const page = fs.readFileSync(path.join(here, '..', '..', 'src', 'main', 'resources', 'dashboard.html'), 'utf8');
    assert.ok(!/className\s*=\s*'notice info'/.test(page), '.info is the 24 px italic circle of the help buttons');
});

test('energy can be shown as kWh or as estimated miles, and the goal always goes out as whole kWh', () => {
    const miles = on => read('unitMiles = ' + on + '; milesPerKwh = 3;');
    miles(false);
    assert.equal(page.fmtEnergy(12.34), '12.3 kWh');
    assert.equal(page.fmtCarPower(7200), '7.20 kW');
    assert.equal(read.call(null, "goalFromBox")(20), 20);
    assert.equal(read.call(null, "goalToBox")(20), 20);
    miles(true);
    assert.equal(page.fmtEnergy(14), '~42 mi');                 // whole miles from 10 up
    assert.equal(page.fmtEnergy(2), '~6.0 mi');                  // one decimal below that
    assert.equal(page.fmtEnergy(0), '~0.0 mi');
    assert.equal(page.fmtEnergy(3.3), '~9.9 mi');
    assert.equal(page.fmtEnergy(3.32), '~10 mi');                // 9.96 must not print as "10.0"
    assert.equal(page.fmtCarPower(7200), '~22 mi/h');
    assert.equal(page.fmtCarPower(0), '0 mi/h');
    assert.equal(read.call(null, "goalFromBox")(40), 13);                      // 40 mi at 3 mi/kWh, rounded to whole kWh
    assert.equal(read.call(null, "goalFromBox")(1), 1);                        // a goal above zero is never rounded down to "off"
    assert.equal(read.call(null, "goalFromBox")(0), 0);
    assert.equal(read.call(null, "goalToBox")(13), 39);
    assert.equal(read.call(null, "goalText")(13), '~39 mi');
    assert.equal(page.goalProgress({ doneKwh: 4, kwh: 13 }), '~12 of ~39 mi range');
    read('milesPerKwh = 3.5;');
    assert.equal(page.fmtEnergy(10), '~35 mi');
    miles(false);
    assert.equal(page.goalProgress({ doneKwh: 4, kwh: 13 }), '4 of 13 kWh');
});

// ---- the Tesla tile and settings ----

test('the Tesla tile says what the car reported, how old it is, and compares with the charger', () => {
    const c = { watch: true, reading: { ageSeconds: 120, battery: 46, limit: 80, charging: 'charging', amps: 18, volts: 239 } };
    const v = page.carView(c, { connected: true, amps: 17.9 });
    assert.equal(v.main, '46%');
    assert.match(v.state, /Charging · 18 A · 4\.30 kW · limit 80%/);
    assert.match(v.note, /Read 2m ago/);
    assert.match(v.note, /charger shows 17\.9 A/);
    assert.equal(v.pct, 46);
    assert.equal(v.limit, 80);
    assert.equal(v.stale, false);
    assert.equal(page.carView({ reading: { ageSeconds: 2, battery: 50 } }, null).note, 'Read just now');
    assert.ok(!/ago ago/.test(v.note));
});

test('an old reading is marked stale and a sleeping car is not shown as charging', () => {
    const old = page.carView({ reading: { ageSeconds: 3600, battery: 50, charging: 'stopped' } }, null);
    assert.equal(old.stale, true);
    assert.match(old.note, /^Last read /);
    const asleep = page.carView({ reading: { ageSeconds: 30, sleep: 2 } }, null);
    assert.equal(asleep.main, 'Asleep');
});

test('before any reading the tile explains why, and a failed read says the car was not reachable', () => {
    assert.match(page.carView({ watch: true }, null).note, /within a minute/);
    assert.match(page.carView({ watch: false }, null).note, /Check now/);
    const failed = page.carView({ watch: true, result: { job: 'status', ok: false, ageSeconds: 5, lines: ['The car was not heard. Move the board closer.'] } }, null);
    assert.equal(failed.state, 'Not reachable');
    assert.match(failed.note, /not heard/);
    assert.match(page.carView({ busy: true, job: 'status' }, null).note, /Reading/);
});

test('pairing progress tells the owner when to tap the key card', () => {
    assert.match(page.carProgressText({ busy: true, job: 'pair', pair: { state: 'finding' } }), /Looking for your car/);
    const waiting = page.carProgressText({ busy: true, job: 'pair', pair: { state: 'waiting', text: 'Tap your key card now.', secondsLeft: 87 } });
    assert.match(waiting, /Tap your key card now\. \(87 s left\)/);
    assert.equal(page.carProgressText({ busy: false }), '');
    assert.match(page.carProgressText({ busy: true, job: 'charge' }), /Sending the command/);
});

test('the VIN is checked in the page the same way as on the board, and travels in the body, not the address', () => {
    const cleanVin = read('cleanVin'), VIN_OK = read('VIN_OK');   // top-level consts are not properties of the context
    assert.equal(cleanVin(' 5yj-ygdee2 mf284667 '), '5YJYGDEE2MF284667');
    assert.ok(VIN_OK.test('5YJYGDEE2MF284667'));
    for (const bad of ['', '5YJYGDEE2MF28466', '5YJYGDEE2MF28466O', '5YJYGDEE2MF28466I', '5YJYGDEE2MF28466Q', '5YJYGDEE2MF28466!']) assert.ok(!VIN_OK.test(bad), bad);
    assert.ok(!/api\/car\/pair\?/.test(html), 'the VIN must never be put in a URL');
    assert.match(script, /carStart\('\/api\/car\/pair', \{ vin \}/);
    assert.match(script, /fields \? await postForm\(url, fields\)/, 'fields go through the body helper');
});

test('every Tesla test button asks before it changes the car', () => {
    const body = name => script.match(new RegExp('async function ' + name + '\\([^)]*\\) \\{([\\s\\S]*?)\\n\\}'))[1];
    for (const name of ['carTest', 'carSetAmps', 'carForget']) assert.match(body(name), /confirm\(/, name + ' must ask first');
    assert.match(body('carRestore'), /carSetAmps\(n\)/, 'restoring goes through the same question');
});

// ---- Control through: Car ----

test('the tile says who is in command, and warns when the car is asleep or out of reach', () => {
    const base = { watch: true, reading: { ageSeconds: 20, battery: 50, charging: 'charging', amps: 12, volts: 239 } };
    assert.equal(page.carView(base, null).control, '');                               // the charger is in command: no line
    const on = page.carView(Object.assign({}, base, { control: { through: true, state: "WattSync is controlling the car's charging.", unreachableSeconds: 0 } }), null);
    assert.match(on.control, /WattSync is controlling/);
    assert.equal(on.controlWarn, false);
    const out = page.carView(Object.assign({}, base, { control: { through: true, state: 'The car cannot be reached. It keeps its last setting; trying again.', unreachableSeconds: 720 } }), null);
    assert.match(out.control, /Out of reach for 12m\./);
    assert.equal(out.controlWarn, true);
    const asleep = page.carView(Object.assign({}, base, { control: { through: true, state: 'The car is asleep. It is not woken from here.', unreachableSeconds: 0 } }), null);
    assert.equal(asleep.controlWarn, true);
});

test('switching to the car asks first, names the car, and needs the confirmation word on the wire', () => {
    const body = script.match(/async function carThrough\([^)]*\) \{([\s\S]*?)\n\}/)[1];
    assert.match(body, /confirm\(/);
    assert.match(body, /carName\(\)/);
    assert.match(body, /confirm=yes/);
    assert.match(body, /select\.value = was/, 'a refusal puts the box back');
    assert.match(script, /async function carOnReturn/);
});

test('the test buttons are switched off while the car is in command', () => {
    assert.match(script, /#car-testing button, #car-testing input/);
    assert.match(script, /b\.disabled = inCommand/);
});

// ---- waking the car ----

test('waking is off until switched on, goes through the API, and manual wakes ask first', () => {
    assert.match(script, /async function carAllowWake/);
    assert.match(script, /\/api\/car\/allowwake\?enabled=/);
    const wake = script.match(/async function carWake\([^)]*\) \{([\s\S]*?)\n\}/)[1];
    assert.match(wake, /confirm\(/);
    assert.match(wake, /carName\(\)/);
    assert.match(page.carProgressText({ busy: true, job: 'wake' }), /Waking the car/);
    assert.match(html, /id="car-allowwake"/);
    assert.ok(!/id="car-allowwake"[^>]*checked/.test(html), 'it must not start switched on');
});

// ---- Tesla mode ----

test('Tesla mode: an older controller that does not report it shows everything as before', () => {
    const v = page.carModeView({ supported: true, paired: true });
    assert.equal(v.known, false);
    assert.equal(v.on, true);
});

test('Tesla mode says what is on or off, that a paired car is kept, and when the board is restarting', () => {
    const off = page.carModeView({ paired: true, mode: { enabled: false, restartPending: false, clockSet: true } });
    assert.equal(off.on, false);
    assert.match(off.note, /Off\. Bluetooth stays off/);
    assert.match(off.note, /paired car is kept/);
    assert.equal(off.needsClock, false);
    const on = page.carModeView({ paired: false, mode: { enabled: true, hotspotUp: true, hotspotClients: 2, clockSet: false } });
    assert.equal(on.on, true);
    assert.match(on.note, /once one is paired/);
    assert.equal(on.clients, 2);
    assert.equal(on.needsClock, true);   // away from home the phone gives the board the time
    const pending = page.carModeView({ mode: { enabled: true, restartPending: true } });
    assert.match(pending.note, /Restarting/);
});

test('the help card for Tesla mode exists', () => {
    assert.ok(read('TIPS').carmode);
});

// ---- the solar meter notices (no vendor names: other makes will follow) ----

test('a meter that was never set up says so, calmly, and locks Eco and Max solar', () => {
    const v = page.meterView({ meterLinked: false, enphaseOk: false, controller: {} });
    assert.equal(v.state, 'none');
    assert.equal(v.title, 'No solar meter paired');
    assert.equal(v.blockSolar, true);
    assert.equal(v.soft, true);
    assert.match(v.tail, /Custom charging still works/);
    assert.equal(v.diag, 'No solar meter paired');
});

test('a meter that is set up but cannot be read says that, and only locks the solar modes once it has been silent for 150 s or the fail-safe has stopped charging', () => {
    const blip = page.meterView({ meterLinked: true, enphaseOk: false, controller: { failSafe: false, hasReading: true, readingAgeSeconds: 60, pollSeconds: 60 } });
    assert.equal(blip.state, 'lost');
    assert.equal(blip.title, 'Unable to read solar meter');
    assert.equal(blip.blockSolar, false);
    assert.equal(blip.soft, false);
    assert.equal(blip.diagCls, 'bad');
    assert.equal(page.meterView({ meterLinked: true, enphaseOk: false, controller: { failSafe: true } }).blockSolar, true);
    // Custom never trips the fail-safe, but a meter silent for over 150 s still greys out Eco and Max solar
    assert.equal(page.meterView({ meterLinked: true, enphaseOk: false, controller: { hasReading: true, readingAgeSeconds: 200, pollSeconds: 60 } }).blockSolar, true);
    assert.equal(page.meterView({ meterLinked: true, enphaseOk: false, controller: { hasReading: false, pollSeconds: 60 } }).blockSolar, true);
});

test('a readable meter says nothing, and an older controller without the flag counts as linked', () => {
    assert.equal(page.meterView({ meterLinked: true, enphaseOk: true, controller: {} }).state, 'ok');
    assert.equal(page.meterView({ enphaseOk: true, controller: {} }).state, 'ok');
    assert.equal(page.meterView({ enphaseOk: false, controller: {} }).state, 'lost');
});

test('in Tesla mode the missing meter points to Custom and never uses the red bar; no vendor is named anywhere', () => {
    const tesla = { car: { mode: { enabled: true } }, controller: {} };
    const none = page.meterView({ ...tesla, meterLinked: false });
    const lost = page.meterView({ ...tesla, meterLinked: true, enphaseOk: false });
    assert.match(none.tail, /Use Custom/);
    assert.match(lost.tail, /Use Custom/);
    assert.equal(lost.soft, true);
    for (const v of [none, lost, page.meterView({ meterLinked: false, controller: {} }), page.meterView({ enphaseOk: false, controller: {} })])
        assert.doesNotMatch(JSON.stringify(v), /enphase|envoy|gateway/i);
});
