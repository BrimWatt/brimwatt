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
    assert.equal(page.goalProgress({ doneKwh: 4, kwh: 13 }), '~12 of ~39 mi');
    read('milesPerKwh = 3.5;');
    assert.equal(page.fmtEnergy(10), '~35 mi');
    miles(false);
    assert.equal(page.goalProgress({ doneKwh: 4, kwh: 13 }), '4 of 13 kWh');
});
