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
    assert.deepEqual({ ...read('MODE_NAME') }, { ECO: 'Eco', MAX_SOLAR: 'Max solar', FAST: 'Custom', CHEAPEST: 'Cheapest hours' });
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
    assert.equal(cleanVin(' 5yj-3e1ea8 kf000001 '), '5YJ3E1EA8KF000001');
    assert.ok(VIN_OK.test('5YJ3E1EA8KF000001'));
    for (const bad of ['', '5YJ3E1EA8KF00000', '5YJ3E1EA8KF00000O', '5YJ3E1EA8KF00000I', '5YJ3E1EA8KF00000Q', '5YJ3E1EA8KF00000!']) assert.ok(!VIN_OK.test(bad), bad);
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

// ---- theme: Match my phone (default), Light, Dark ----

test('the theme choice defaults to matching the phone; only light and dark are ever kept', () => {
    assert.equal(page.themeChoice(null), 'auto');
    assert.equal(page.themeChoice(undefined), 'auto');
    assert.equal(page.themeChoice('auto'), 'auto');
    assert.equal(page.themeChoice('nonsense'), 'auto');
    assert.equal(page.themeChoice('light'), 'light');
    assert.equal(page.themeChoice('dark'), 'dark');
});

test('the dark colours are defined once for a forced Dark and again for a phone that is dark, with the same values, and cover every light variable', () => {
    const vars = block => Object.fromEntries([...block.matchAll(/--([a-z0-9-]+):\s*([^;]+);/g)].map(m => [m[1], m[2].trim()]));
    const light = vars(html.match(/:root \{([^}]*)\}/)[1]);
    const forced = vars(html.match(/:root\[data-theme="dark"\] \{([^}]*)\}/)[1]);
    const auto = vars(html.match(/:root:not\(\[data-theme="light"\]\) \{([^}]*)\}/)[1]);
    assert.deepEqual(auto, forced, 'Dark and the phone-is-dark rule must stay identical');
    for (const name of Object.keys(light)) assert.ok(name in forced, 'a dark value for --' + name);
    assert.notEqual(forced.bg, light.bg);
    assert.notEqual(forced.card, light.card);
});

test('no light-only colour is left in the styles: everything that changes with the theme goes through a variable', () => {
    const css = html.slice(html.indexOf('<style>'), html.indexOf('</style>'));
    const outsideRoot = css.replace(/:root[^{]*\{[^}]*\}/g, '');   // the palettes themselves may hold literals
    for (const literal of ['#f8fafc', '#eef1f4', '#cbd5e1', '#eaedf1', '#e2e6ea', '#def7ec', '#ffe3e3', '#fef3c7', '#475569', '#374151'])
        assert.ok(!outsideRoot.includes(literal), literal + ' should be a theme variable');
    assert.ok(!/background:\s*#fff\b/.test(outsideRoot.replace(/::after[^}]*\}/g, '')), 'a white background should use --card or --field');
});

test('the chart and the flow arrows take their colours from the theme, not from fixed values', () => {
    const js = html.slice(html.indexOf('</style>'));
    for (const literal of ['#eda100', '#e34948', '#2a78d6', '#898781', '#e5e7eb', '#dbeafe', '#f1f5f9']) assert.ok(!js.includes("'" + literal + "'"), literal + ' in the script');
});

// ---- waiting states: the reason in the title, the numbers, a countdown ----
const idle = mutate => {
    const d = base();
    d.charger = { status: 'PREPARING', amps: 0, limitAmps: 0 };
    d.controller.reason = 'waiting for surplus';
    mutate(d);
    return page.waitView(d);
};

test('countdowns are short and say nothing when there is no timer', () => {
    assert.equal(page.fmtWait(-1), '');
    assert.equal(page.fmtWait(undefined), '');
    assert.equal(page.fmtWait(0), 'under a minute');
    assert.equal(page.fmtWait(45), 'under a minute');
    assert.equal(page.fmtWait(60), 'about 1 min');
    assert.equal(page.fmtWait(130), 'about 2 min');
});

test('Eco: the house is using the solar, and the title says so', () => {
    const v = idle(d => { d.power.solarWatts = 5000; d.power.homeWatts = 4900; d.controller.gridAllowanceWatts = 1000; });
    assert.equal(v.title, 'Waiting: home is using the solar');
    assert.equal(v.sub, 'Home is using 4.9 kW of your 5.0 kW solar. Needs 0.7 kW spare to start.');
    assert.equal(v.cls, 'wait');
});

test('Eco: just stopped, the title says it is waiting to restart and for how long', () => {
    const v = idle(d => { d.power.solarWatts = 5000; d.power.homeWatts = 4900; d.controller.gridAllowanceWatts = 1000; d.controller.waitSeconds = 70; });
    assert.equal(v.title, 'Waiting to restart');
    assert.match(v.sub, /Restart possible in about 1 min\./);
});

test('Max solar ignores the house and talks about the solar alone', () => {
    const v = idle(d => { d.currentMode = 'MAX_SOLAR'; d.power.solarWatts = 100; d.power.homeWatts = 4200; d.controller.gridAllowanceWatts = 1000; });
    assert.equal(v.title, 'Waiting for more solar');
    assert.equal(v.sub, 'Solar is 0.1 kW. Needs about 0.4 kW to start.');
});

test('enough solar and the planner is confirming it lasts', () => {
    const v = idle(d => { d.controller.reason = 'surplus present, confirming it lasts'; d.controller.waitSeconds = 120; });
    assert.equal(v.title, 'Starting soon');
    assert.equal(v.sub, 'Enough solar. Starts in about 2 min if it holds.');
    assert.equal(idle(d => { d.controller.reason = 'surplus present, confirming it lasts'; }).sub, 'Enough solar. Starts shortly if it holds.');
});

test('night: waiting for sunrise', () => {
    const v = idle(d => { d.daylight.open = false; });
    assert.equal(v.title, 'Waiting for sunrise');
});

test('a dip while charging warns and counts down to the stop', () => {
    const d = base();
    d.controller.reason = 'dip: at the minimum, waiting before stopping';
    d.controller.waitSeconds = 200;
    d.power = { voltage: 240, solarWatts: 5000, homeWatts: 4600, chargerWatts: 1440, gridWatts: 0 };
    d.charger = { status: 'CHARGING', amps: 6, limitAmps: 6 };
    const v = page.waitView(d);
    assert.equal(v.title, 'Low solar, charging at 6 A');
    assert.equal(v.cls, 'warn');
    assert.equal(v.sub, 'Home is using 4.6 kW of your 5.0 kW solar. Stops in about 3 min if it stays low.');
    d.controller.reason = 'track surplus';
    assert.equal(page.waitView(d), null);
});

test('waiting view stays out of the way for other states', () => {
    assert.equal(idle(d => { d.paused = true; }), null);
    assert.equal(idle(d => { d.currentMode = 'FAST'; }), null);
    assert.equal(idle(d => { d.charger.status = 'AVAILABLE'; }), null);
    assert.equal(idle(d => { d.charger.status = 'SUSPENDED_EV'; }), null);
    assert.equal(idle(d => { d.controller.failSafe = true; }), null);
    assert.equal(idle(d => { d.controller.hasReading = false; }), null);
});

// ---- the day graph: export hangs below zero only on days that have some ----
test('the chart has no room below zero unless there is export', () => {
    assert.equal(page.exportFloor(0, 8000), 0);
    assert.equal(page.exportFloor(60, 8000), 0);            // import only
    assert.equal(page.exportFloor(-50, 8000), 0);           // a trickle is not worth the space
    assert.equal(page.exportFloor(-2400, 8000), -2500);     // 500 W steps, so a small export does not waste half the chart
    assert.equal(page.exportFloor(-900, 8000), -1000);
    assert.equal(page.exportFloor(-1437, 4000), -1500);
});

test('export never takes more than half the height of the top side', () => {
    assert.equal(page.exportFloor(-20000, 8000), -4000);
    assert.equal(page.exportFloor(-20000, 10000), -5000);
});

test('the new graph colours exist in every palette and the dark ones are not the near-invisible greys', () => {
    const html = fs.readFileSync(new URL('../../src/main/resources/dashboard.html', import.meta.url), 'utf8');
    for (const v of ['--homeline', '--stop', '--zero']) {
        assert.equal((html.match(new RegExp(v + ':', 'g')) || []).length, 3, v + ' must be in the light palette and both dark ones');
    }
    assert.ok(!/--line: #263244/.test(html) && !/--ribbon: #1b2638/.test(html) && !/--band: #1e3a5f/.test(html), 'old dark greys are gone');
});

// ---- the day graph starts at the first reading, not at 6 am ----
const H = h => h * 3600;
const dayWith = (firstHour, extra = {}) => Object.assign({ dayStart: 0, daysAgo: 0, now: H(14.5), daylight: { start: H(7), end: H(17) },
    samples: [[H(firstHour), 0, 0, 0, 0, 0, 0], [H(14.5), 1, 1, 0, 0, 0, 0]] }, extra);

test('the graph starts just before the first reading', () => {
    assert.equal(page.chartSpan(dayWith(7.5))[0], H(7.5) - 300);
    assert.equal(page.chartSpan(dayWith(9))[0], H(9) - 300);
});

test('the graph ends where the daylight window ends, so the shaded hours reach both edges', () => {
    assert.equal(page.chartSpan(dayWith(7.5))[1], H(17));
    assert.equal(page.chartSpan(dayWith(7.5, { now: H(19.2), samples: [[H(7.5), 0, 0, 0, 0, 0, 0], [H(19.2), 1, 1, 0, 0, 0, 0]] }))[1], H(19.2) + 600);   // later readings are never cut off
    assert.equal(page.chartSpan(dayWith(7.5, { daylight: null }))[1], H(20));
});

test('the graph never starts before the day itself', () => {
    assert.equal(page.chartSpan(dayWith(0))[0], 0);
});

test('quiet overnight readings do not stretch the graph, but a night charge does', () => {
    const quiet = [[H(0), 0, 300, 300, 0, 0, 0], [H(3), 0, 320, 320, 0, 0, 0]];
    const day = [[H(7.7), 200, 300, 100, 0, 0, 0], [H(14.5), 5000, 400, -4000, 0, 0, 0]];
    assert.equal(page.chartSpan(dayWith(0, { samples: quiet.concat(day) }))[0], H(7.7) - 300);
    const nightCharge = [[H(0), 0, 300, 300, 0, 0, 0], [H(1), 0, 300, 7500, 7200, 0, 1]];
    assert.equal(page.chartSpan(dayWith(0, { samples: nightCharge.concat(day) }))[0], H(1) - 300);
});

test('a running charge counts as activity, a pause alone does not', () => {
    const charging = [[H(2), 0, 300, 300, 0, 0, 1], [H(8), 100, 300, 200, 0, 0, 0]];
    assert.equal(page.chartSpan(dayWith(0, { samples: charging }))[0], H(2) - 300);
    const paused = [[H(2), 0, 300, 300, 0, 0, 2], [H(8), 10, 300, 290, 0, 0, 0]];
    assert.equal(page.chartSpan(dayWith(0, { samples: paused }))[0], H(2) - 300);   // nothing active at all: starts at the first reading
    const pausedThenSun = [[H(2), 0, 300, 300, 0, 0, 2], [H(8), 400, 300, -100, 0, 0, 0]];
    assert.equal(page.chartSpan(dayWith(0, { samples: pausedThenSun }))[0], H(8) - 300);
});

test('a day with no active reading starts at its first reading', () => {
    const none = [[H(3), 0, 300, 300, 0, 0, 0], [H(14.5), 10, 300, 290, 0, 0, 0]];
    assert.equal(page.chartSpan(dayWith(0, { samples: none }))[0], H(3) - 300);
});

test('the new animations only run when the phone has not asked for reduced motion', () => {
    const css = html.match(/<style>([\s\S]*?)<\/style>/)[1];
    const block = css.match(/@media \(prefers-reduced-motion: no-preference\) \{\s*\n\s*\.hero \.title\.swap[\s\S]*?\n        \}\n/)[0];
    for (const name of ['titlein', 'breathe', 'wspin', 'boltpop', 'hubglow', 'sweep', 'rowin', 'sheetup', 'tipin'])
        assert.ok(block.includes('animation: ' + name), name + ' is started inside the no-preference block');
    const outside = css.replace(block, '');
    for (const name of ['titlein', 'breathe', 'wspin', 'hubglow', 'sweep', 'rowin', 'sheetup', 'tipin'])
        assert.ok(!new RegExp('animation:\\s*' + name).test(outside), name + ' must not run outside it');
});

test('the flow dots and number glide are skipped when motion is reduced', () => {
    const ctx = load();
    assert.equal(ctx.motionOk(), false, 'the stand-in page has no real motion preference, so everything stays still');
});

// ---- the value card ----
// Day rows are [epochDay, solarKwh, carKwh, carSolarKwh, importKwh?, exportKwh?, savedDollars (-1 = none recorded)].
// 20000 is Fri 4 Oct 2024 in UTC.
const valueDays = (extra = []) => ({ today: 20000, days: [[20000, 0, 99, 99, 0, 0, 99], [19999, 0, 5, 5, 0, 0, 1.0], [19997, 0, 10, 4, 0, 0, -1], [19990, 0, 8, 8, 0, 0, 2.0], ...extra] });
const totalsToday = { carKwh: 12, carSolarKwh: 9, savedDollars: 3.0 };

test('Today is the live totals, not the stale row for today', () => {
    const v = page.valueSummary('today', valueDays(), totalsToday);
    assert.equal(v.car, 12);
    assert.equal(v.saved, 3);
    assert.equal(v.share, 75);
});

test('Week and Month add up the days in the period, with today taken from the live totals', () => {
    const w = page.valueSummary('week', valueDays(), totalsToday);
    assert.equal(w.car, 12 + 5 + 10);                          // today, yesterday and 19997; 19990 is 10 days back
    assert.equal(Math.round(w.sun * 100) / 100, 9 + 5 + 4);
    assert.equal(Math.round(w.saved * 100) / 100, 3 + 1 + 4 * 30 / 100, 'a day with no recorded saving is estimated at the normal price');
    const m = page.valueSummary('month', valueDays(), totalsToday);
    assert.equal(m.car, 12 + 5 + 10 + 8);
});

test('Year counts the twelve months ending this month, the same window as the graph bars', () => {
    const rows = [[19950, 0, 6, 6, 0, 0, 1.5], [19700, 0, 7, 7, 0, 0, 2.0], [19600, 0, 50, 50, 0, 0, 9]];   // 15 Aug 2024, 8 Dec 2023, 31 Aug 2023
    const y = page.valueSummary('year', valueDays(rows), totalsToday);
    assert.equal(y.car, 12 + 5 + 10 + 8 + 6 + 7, 'the window starts 1 Nov 2023, so 31 Aug 2023 is left out');
});

test('the sun never counts for more than the car took, and no charging means no share', () => {
    const v = page.valueSummary('today', { today: 20000, days: [] }, { carKwh: 4, carSolarKwh: 9, savedDollars: -1 });
    assert.equal(v.sun, 4);
    assert.equal(v.share, 100);
    const none = page.valueSummary('today', { today: 20000, days: [] }, { carKwh: 0, carSolarKwh: 0, savedDollars: 0 });
    assert.equal(none.share, null);
    assert.equal(none.car, 0);
});

test('the card for a finished charge says what was added, the solar share and the saving', () => {
    assert.equal(page.sessionDoneText({ kwh: 10.4, sunPct: 94.2, saved: 1.8, sunKwh: 9.8 }), '10.4 kWh added · 94.2% from solar · saved $1.80');
    assert.equal(page.sessionDoneText({ kwh: 5, sunPct: 140, saved: -1, sunKwh: 5 }), '5.0 kWh added · 100% from solar · saved $1.50', 'no recorded saving is estimated, the share is capped');
});

test('the Car tile shows its power in the other unit under the big number', () => {
    const ctx = load();
    vm.runInContext('unitMiles = false; milesPerKwh = 3;', ctx);
    assert.equal(ctx.fmtCarPower(2640), '2.64 kW');
    assert.equal(ctx.fmtCarPowerOther(2640), '~7.9 mi/h', 'kW on top, miles per hour under it');
    assert.equal(ctx.fmtCarPowerOther(0), '0 mi/h');
    vm.runInContext('unitMiles = true;', ctx);
    assert.equal(ctx.fmtCarPower(2640), '~7.9 mi/h');
    assert.equal(ctx.fmtCarPowerOther(2640), '2.64 kW', 'miles per hour on top, kW under it');
    assert.equal(ctx.fmtCarPowerOther(850), '850 W');
});

test('a number that is only the set limit gets a ~ and miles keep the one they have', () => {
    const ctx = load();
    assert.equal(ctx.fmtApprox('2.64 kW'), '~2.64 kW');
    assert.equal(ctx.fmtApprox('850 W'), '~850 W');
    assert.equal(ctx.fmtApprox('~7.9 mi/h'), '~7.9 mi/h');
    assert.equal(ctx.fmtApprox('0 mi/h'), '0 mi/h');
    assert.equal(ctx.fmtApprox('0 W'), '0 W');
});

// ---- the comparison with gas ----
const near = (a, b, tol = 0.01) => assert.ok(Math.abs(a - b) <= tol, a + ' should be about ' + b);

test('what the same miles would have cost on gas, on the grid only, and what was paid', () => {
    // a month: 268 kWh into the car, $61 saved by the sun, 30 c/kWh, 3 miles per kWh, a 28 mpg gas car at $4.60 a gallon
    const g = page.gasCompare(268, 61, 30, 3, 28, 4.6);
    near(g.miles, 804);
    near(g.gasCost, 804 / 28 * 4.6);
    near(g.gridOnly, 80.4);
    near(g.paid, 80.4 - 61);
    near(g.solar, 61);
    near(g.evVsGas, 804 / 28 * 4.6 - 80.4);
    near(g.less, g.evVsGas + g.solar, 0.001, 'the two reasons add up to the whole saving');
});

test('the sun is never credited with more than the grid-only cost, so paid is never negative', () => {
    const g = page.gasCompare(10, 12, 30, 3, 28, 4.6);   // peak pricing can make the recorded saving larger than 10 kWh at the normal price
    near(g.gridOnly, 3);
    near(g.solar, 3);
    near(g.paid, 0);
});

test('an expensive grid and a thrifty gas car can come out the wrong way round, and the numbers say so', () => {
    const g = page.gasCompare(10, 0, 80, 2, 40, 3);
    near(g.gasCost, 1.5);
    near(g.gridOnly, 8);
    near(g.evVsGas, -6.5);
    near(g.less, -6.5);
});

test('dollars in the comparison: cents below $100, whole dollars above, a proper minus', () => {
    assert.equal(page.fmtDollars(112.69), '$113');
    assert.equal(page.fmtDollars(19.4), '$19.40');
    assert.equal(page.fmtDollars(-6.5), '−$6.50');
    assert.equal(page.fmtDollars(0), '$0.00');
});

// ---- Cheapest hours ----
const cheapData = mutate => {
    const d = base();
    d.currentMode = 'CHEAPEST'; d.meterLinked = false; d.maxAmps = 32; d.rateCents = 30; d.peakRateCents = 52;
    d.peakStartHour = 16; d.peakEndHour = 21; d.peakWeekends = true;
    d.charger = { status: 'PREPARING', amps: 0, limitAmps: 0 };
    d.controller = Object.assign({}, d.controller, { expensiveNow: true, secondsToPriceChange: 2 * 3600 + 600 });
    mutate(d);
    return d;
};

test('price view: expensive hours show the peak price and when it gets cheap', () => {
    const v = page.priceView(cheapData(() => {}));
    assert.equal(v.expensive, true);
    assert.equal(v.price, '52¢');
    assert.equal(v.nextLabel, 'Cheap at');
    assert.equal(v.nextValue, '9:00 pm');
    assert.equal(v.nextSub, 'in 2 h');
    assert.equal(v.line, 'Charges at up to 32 A, except 4:00 pm – 9:00 pm every day.');
});

test('price view: cheap hours show the normal price and when it gets expensive; weekdays-only is said', () => {
    const v = page.priceView(cheapData(d => { d.controller.expensiveNow = false; d.controller.secondsToPriceChange = 25 * 60; d.peakWeekends = false; }));
    assert.equal(v.price, '30¢');
    assert.equal(v.nextLabel, 'Peak at');
    assert.equal(v.nextValue, '4:00 pm');
    assert.equal(v.nextSub, 'in 25 min');
    assert.ok(v.line.endsWith('on weekdays.'));
});

test('price view: a change days away is said in days, and no change at all is said too', () => {
    assert.equal(page.priceView(cheapData(d => { d.controller.expensiveNow = false; d.controller.secondsToPriceChange = 66 * 3600; })).nextSub, 'in 2.8 days');
    assert.equal(page.priceView(cheapData(d => { d.controller.secondsToPriceChange = -1; })).nextSub, 'none set');
});

test('the headline waits for cheaper hours with the start time, and stays quiet once charging', () => {
    const h = page.waitView(cheapData(() => {}));
    assert.equal(h.title, 'Waiting for cheaper hours');
    assert.ok(h.sub.startsWith('Charging starts at 9:00 pm.'));
    assert.equal(page.waitView(cheapData(d => { d.controller.expensiveNow = false; })), null);
    assert.equal(page.waitView(cheapData(d => { d.charger = { status: 'CHARGING', amps: 30, limitAmps: 30 }; d.controller.expensiveNow = false; })), null);
    assert.equal(page.waitView(cheapData(d => { d.paused = true; })), null);
});

test('Cheapest hours is a flat mode like Custom: no solar hours, no meter notice, no resting text', () => {
    const d = cheapData(d2 => { d2.daylight = { open: false }; });
    assert.equal(page.flatMode(d), true);
    assert.equal(page.isResting(d), false);
    assert.equal(page.flatMode(base()), false);
});

// ---- Your impact: score, streaks, badges, share card ----
const IMP_TODAY = 20000, IMP_START = IMP_TODAY * 86400;   // day 20000 is a Friday
const impSite = { dayStart: IMP_START, today: IMP_TODAY };
const impDays = rows => ({ today: IMP_TODAY, days: rows });
const impCtx = extra => Object.assign({ period: 'week', days: null, totals: null, sessions: [], site: impSite, rateCents: 30, peakStart: 16, peakEnd: 21,
    weekends: true, offpeakApplies: true, mpk: 3, mpg: 28, price: 4.6, meterLinked: true }, extra);
const impSession = (dayOffset, fromHour, toHour, kwh) => ({ start: IMP_START + dayOffset * 86400 + fromHour * 3600, end: IMP_START + dayOffset * 86400 + toHour * 3600, kwh });

test('site time: hour and weekday come from the day start, not from the phone', () => {
    const m = page.siteMoment(IMP_START + 17 * 3600, impSite);
    assert.deepEqual([m.day, m.hour, m.weekday], [IMP_TODAY, 17, 5]);
    const b = page.siteMoment(IMP_START - 3600, impSite);
    assert.deepEqual([b.day, b.hour, b.weekday], [IMP_TODAY - 1, 23, 4]);
});

test('expensive hours wrap past midnight and skip weekends when told to', () => {
    const at = (hour, weekday) => ({ hour, weekday });
    assert.equal(page.isExpensiveHour(at(23, 2), 21, 6, true), true);
    assert.equal(page.isExpensiveHour(at(5, 2), 21, 6, true), true);
    assert.equal(page.isExpensiveHour(at(6, 2), 21, 6, true), false);
    assert.equal(page.isExpensiveHour(at(17, 2), 16, 21, true), true);
    assert.equal(page.isExpensiveHour(at(21, 2), 16, 21, true), false);
    assert.equal(page.isExpensiveHour(at(17, 6), 16, 21, false), false);   // Saturday, weekends off
    assert.equal(page.isExpensiveHour(at(17, 6), 16, 21, true), true);
});

test('a charge is split by the hour: 15 to 18 with a 16 to 21 peak has two thirds in the peak', () => {
    const d = page.perDayPeak([impSession(0, 15, 18, 6)], impSite, 16, 21, true, 0);
    assert.equal(d.length, 1);
    assert.equal(Math.round(d[0].kwh * 100) / 100, 6);
    assert.equal(Math.round(d[0].peakKwh * 100) / 100, 4);
});

test('the score drops a part that does not apply and rescales the rest', () => {
    const both = page.impactScore(0.72, 0.9);
    assert.equal(both.score, 79); assert.equal(both.grade, 'B'); assert.equal(both.parts.length, 2);
    assert.equal(page.impactScore(0.72, null).score, 72);
    assert.equal(page.impactScore(null, 0.96).score, 96);
    assert.equal(page.impactScore(null, null), null);
    assert.deepEqual([90, 89, 75, 74, 60, 59].map(n => page.gradeOf(n)), ['A', 'B', 'B', 'C', 'C', 'D']);
});

test('planet and wallet numbers match the worked week: 78 kWh, 56 from the sun', () => {
    const n = page.impactNumbers(78, 56, 30, 3, 28, 4.6);
    assert.equal(Math.round(n.miles), 234);
    assert.equal(Math.round(n.co2Avoided), 69);
    assert.equal(Math.round(n.paid * 100) / 100, 6.6);
    assert.equal(Math.round(n.cheaperBy), 32);
});

test('milestones: next target, last one earned, never past the top', () => {
    assert.deepEqual(JSON.parse(JSON.stringify(page.ladderStep(0))), { earned: 0, next: 50, pct: 0 });
    assert.deepEqual(JSON.parse(JSON.stringify(page.ladderStep(168))), { earned: 100, next: 250, pct: 67 });
    assert.equal(page.ladderStep(6000).pct, 100);
});

test('sunny-day streak: skips days with no charge and days the sun could not cover, breaks on a grid day, ignores an unfinished today', () => {
    const days = impDays([[19999, 20, 10, 8, 0, 0, 1], [19998, 20, 0, 0, 0, 0, 0], [19997, 5, 10, 2, 0, 0, 0], [19996, 20, 10, 9, 0, 0, 1], [19995, 20, 10, 2, 0, 0, 0]]);
    const v = page.impactView(impCtx({ days, totals: { solarKwh: 3, carKwh: 10, carSolarKwh: 1, savedDollars: 0 } }));
    assert.equal(v.streak.label, 'Sunny-day streak');
    assert.equal(v.streak.count, 2);
});

test('impact view with solar: score from the sun and the off-peak share, CO2 on the card', () => {
    const days = impDays([[19999, 20, 10, 8, 0, 0, 1], [19998, 20, 10, 9, 0, 0, 1]]);
    const sessions = [impSession(-1, 9, 12, 10), impSession(-2, 9, 12, 10)];
    const v = page.impactView(impCtx({ days, sessions }));
    assert.equal(v.noSolar, false);
    assert.ok(Math.abs(v.solarShare - 0.85) < 1e-9);
    assert.equal(v.offShare, 1);
    assert.equal(v.score.parts.length, 2);
    assert.equal(v.card.gradeLabel, 'Solar Score');
    assert.equal(v.card.b.label, 'CO2 avoided vs gas');
});

test('impact view with no solar: Charge Score, peak-free streak, money and miles on the card, no CO2', () => {
    const days = impDays([[19999, 0, 10, 0, 10, 0, 0], [19998, 0, 10, 0, 10, 0, 0], [19997, 0, 10, 0, 10, 0, 0]]);
    const sessions = [impSession(-1, 22, 23, 10), impSession(-2, 17, 19, 10), impSession(-3, 22, 23, 10)];   // the middle one sits in the peak
    const v = page.impactView(impCtx({ days, sessions }));
    assert.equal(v.noSolar, true);
    assert.equal(v.solarShare, null);
    assert.equal(v.score.parts.length, 1);
    assert.equal(v.score.parts[0].key, 'offpeak');
    assert.equal(v.streak.label, 'Peak-free streak');
    assert.equal(v.streak.count, 1);   // yesterday is clean, the day before is not
    assert.equal(v.card.gradeLabel, 'Charge Score');
    assert.ok(!JSON.stringify(v.card).includes('CO2'));
    assert.equal(v.card.a.label, 'less than gas');
});

test('impact view with no peak price and not Cheapest hours: the off-peak part is left out', () => {
    const days = impDays([[19999, 20, 10, 8, 0, 0, 1]]);
    const v = page.impactView(impCtx({ days, offpeakApplies: false, sessions: [impSession(-1, 17, 19, 10)] }));
    assert.equal(v.offShare, null);
    assert.equal(v.score.parts.length, 1);
    assert.equal(v.badges.some(b => b.key === 'peakfree'), false);
});

test('a period with no charging is empty and has no score', () => {
    const v = page.impactView(impCtx({ days: impDays([[19999, 20, 0, 0, 0, 0, 0]]) }));
    assert.equal(v.empty, true);
    assert.equal(v.score, null);
});

test('the Impact tile and its buttons exist and have help text', () => {
    ['impact', 'imp-tab-week', 'imp-tab-month', 'imp-body', 'imp-empty', 'imp-score', 'imp-grade', 'imp-name', 'imp-parts', 'imp-nums', 'imp-streak', 'imp-streak-n', 'imp-streak-l', 'imp-badges', 'imp-share']
        .forEach(id => assert.ok(html.includes('id="' + id + '"'), id));
    assert.ok(read('typeof TIPS.impact') === 'object');
    assert.ok(html.includes('onclick="shareImpact()"') && html.includes("onclick=\"setImpactPeriod('month')\""));
});
