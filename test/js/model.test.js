var test = require('node:test');
var assert = require('node:assert/strict');
var M = require('../../planner/model.js');
var codec = require('../../src/pkjs/codec.js');
var V = require('../build/vectors.json');

test('a new item of every kind validates once it has a name', function () {
  [M.KIND.ROUTINE, M.KIND.REMINDER, M.KIND.TASK, M.KIND.EVENT].forEach(function (k) {
    var it = M.newItem(k, 9, 20384);
    assert.equal(it.id, 9);
    assert.equal(it.kind, k);
    assert.deepEqual(M.validate(it), ['Give it a name.']);
    it.name = 'Something';
    assert.deepEqual(M.validate(it), [], 'kind ' + k);
    assert.deepEqual(codec.unpackItem(codec.packItem(it)).name, 'Something');  // the watch can read it
  });
});

test('validate reports what the watch would refuse', function () {
  var r = M.newItem(M.KIND.REMINDER, 1, 0);
  r.name = 'Water';
  r.until = r.at - 10;
  assert.ok(M.validate(r).indexOf('The window ends before it starts.') >= 0);
  r.until = r.at + 60;
  r.every = 0;
  assert.ok(M.validate(r).indexOf('Repeat at least every minute.') >= 0);
  r.every = 30;
  r.days = 0;
  assert.ok(M.validate(r).indexOf('Pick at least one day.') >= 0);

  var rt = M.newItem(M.KIND.ROUTINE, 2, 0);
  rt.name = 'Morning';
  for (var i = 0; i < 21; i++) rt.steps.push(M.newStep(M.STEP.MANUAL, 0));
  rt.steps.forEach(function (s, j) { s.name = 'Step ' + j; });
  assert.ok(M.validate(rt).indexOf('At most 20 steps.') >= 0);
  rt.steps = [M.newStep(M.STEP.NUMBER)];
  rt.steps[0].name = 'Weight';
  rt.steps[0].num = { step: 1, min: 900, max: 300, decimals: 1, unit: 'kg' };
  assert.ok(M.validate(rt).indexOf('Step 1: the lowest value is above the highest.') >= 0);
  rt.steps = [M.newStep(M.STEP.TIMED)];
  rt.steps[0].name = 'Plank';
  rt.steps[0].amount.start = 0;
  assert.ok(M.validate(rt).indexOf('Step 1 needs a length.') >= 0);
  rt.steps = [M.newStep(M.STEP.MANUAL)];
  assert.ok(M.validate(rt).indexOf('Step 1 needs a name.') >= 0);
});

test('names are cut at 31 bytes the same way the phone codec cuts them', function () {
  ['Stretch hamstrings and hips please', 'Café with Zoë 🎂 and the long name',
   'abcdefghijklmnopqrstuvwxyz0123é', '🌱🌱🌱🌱🌱🌱🌱🌱']
    .forEach(function (s) {
      assert.equal(M.utf8Cut(s, 31), codec.utf8Cut(s, 31));
      assert.ok(M.utf8Bytes(M.utf8Cut(s, 31)) <= 31);
    });
  var it = M.newItem(M.KIND.TASK, 3, 0);
  it.name = 'Stretch hamstrings and hips please';
  assert.equal(M.clean(it).name, 'Stretch hamstrings and hips ple');
});

test('ladder presets match the watch', function () {
  assert.deepEqual(M.LADDERS.gentle, [1, 0]);
  assert.deepEqual(M.LADDERS.normal, [7, 1, 0]);
  assert.deepEqual(M.LADDERS.persistent, [21, 7, 3, 1, 0, -1]);
  assert.deepEqual(V.items[2].json.ladder, M.LADDERS.persistent);  // the C preset, through the vector
  assert.equal(M.ladderName([7, 1, 0]), 'normal');
  assert.equal(M.ladderName([2, 0]), 'custom');
});

test('dates and times convert like the watch', function () {
  V.days.forEach(function (d) {
    assert.equal(M.isoToDay(d.iso), d.day, d.iso);
    assert.equal(M.dayToISO(d.day), d.iso);
  });
  assert.equal(M.minToHHMM(420), '07:00');
  assert.equal(M.minToHHMM(1439), '23:59');
  assert.equal(M.hhmmToMin('09:30'), 570);
  assert.equal(M.hhmmToMin('bad'), null);
});

test('scaled numbers read and write with their decimals', function () {
  assert.equal(M.fromScaled(824, 1), '82.4');
  assert.equal(M.fromScaled(-40, 2), '-0.40');
  assert.equal(M.fromScaled(12, 0), '12');
  assert.equal(M.toScaled('82.4', 1), 824);
  assert.equal(M.toScaled('82', 1), 820);
  assert.equal(M.toScaled('0.4', 2), 40);
  assert.equal(M.toScaled('x', 1), null);
});

test('CSV exports quote what needs quoting', function () {
  var logs = [{ t: 1761200000, item: 1, step: 2, v: 824, label: 'Morning routine · Weight', unit: 'kg', dec: 1 },
              { t: 1761203600, item: 3, step: 255, v: 12, label: 'Pushups, "max"', unit: 'reps', dec: 0 }];
  var csv = M.csvNumbers(logs, 0).split('\n');
  assert.equal(csv[0], 'date,time,item,value,unit');
  assert.equal(csv[1], '2025-10-23,06:13,Morning routine · Weight,82.4,kg');
  assert.equal(csv[2], '2025-10-23,07:13,"Pushups, ""max""",12,reps');
  var hist = [{ day: 20384, out: [{ id: 1, status: 2, count: 0, missed: 0, name: 'Morning routine', kind: 0 },
                                  { id: 2, status: 0, count: 6, missed: 2, name: 'Drink water', kind: 1 }] }];
  var days = M.csvOutcomes(hist).split('\n');
  assert.equal(days[0], 'date,item,result,count,missed');
  assert.equal(days[1], '2025-10-23,Morning routine,done,0,0');
  assert.equal(days[2], '2025-10-23,Drink water,not done,6,2');
});

test('notes export as plain text with their reason when missed', function () {
  var txt = M.textNotes([{ id: 1761200000, len: 72, name: 'Garden ideas', status: 0, reason: 0, text: 'Plant beans.' },
                         { id: 1761203600, len: 3, name: '', status: 1, reason: 5, text: '' }], 0);
  assert.ok(txt.indexOf('Thu 23 Oct 2025 06:13 · 1:12 · Garden ideas\nPlant beans.') === 0, txt);
  assert.ok(txt.indexOf('Thu 23 Oct 2025 07:13 · 0:03 · missed: phone not reachable') > 0, txt);
});

test('times read in the offset in force at that moment, not today’s', function () {
  var was = process.env.TZ;
  process.env.TZ = 'Europe/London';
  try {
    var winter = Date.UTC(2027, 1, 14, 23, 40) / 1000;  // 23:40 GMT
    var summer = Date.UTC(2027, 6, 14, 23, 40) / 1000;  // 00:40 BST the next day
    var w = M.local(winter), s = M.local(summer);
    assert.equal(M.minToHHMM(w.min), '23:40');
    assert.equal(w.date, 14);
    assert.equal(M.minToHHMM(s.min), '00:40');
    assert.equal(s.date, 15);
    assert.equal(s.day - w.day, 151);
    var csv = M.csvNumbers([{ t: winter, label: 'Weight', v: 824, dec: 1, unit: 'kg' },
      { t: summer, label: 'Weight', v: 820, dec: 1, unit: 'kg' }]).split('\n');
    assert.equal(csv[1], '2027-02-14,23:40,Weight,82.4,kg');
    assert.equal(csv[2], '2027-07-15,00:40,Weight,82.0,kg');
    assert.match(M.noteHeading({ id: winter, len: 5 }), /^Sun 14 Feb 2027 23:40/);
  } finally {
    if (was === undefined) delete process.env.TZ; else process.env.TZ = was;
  }
});

var D = 20749;  // a Friday, as in the C tests

test('ramps step up like the watch, and rebase keeps today\'s value', function () {
  var q = { start: 20, cap: 0, since: D, inc: 1, every: 7 };
  assert.equal(M.valueAt(q, D - 3), 20);
  assert.equal(M.valueAt(q, D + 70), 30);
  assert.equal(M.valueAt({ start: 20, cap: 25, since: D, inc: 1, every: 7 }, D + 70), 25);
  var r = M.rebase(q, 2, 7, D + 70); assert.equal(M.valueAt(r, D + 70), 30); assert.equal(M.valueAt(r, D + 77), 32);
  var p = M.rebase(q, 1, 14, D + 73); assert.equal(M.valueAt(p, D + 73), 30); assert.equal(M.valueAt(p, D + 84), 31);
  var f = M.rebase(q, 1, 2, D + 76); assert.equal(M.valueAt(f, D + 76), 30);  // a shorter period never jumps today
  assert.equal(M.valueAt(f, D + 77), 31);
  var s = M.setToday(q, 25, D + 73); assert.deepEqual([s.start, s.since], [25, D + 73]);
  assert.equal(q.start, 20);  // the ramp passed in is left alone
});

test('new steps carry ramps, a rhythm and no alternative', function () {
  var t = M.newStep(M.STEP.TIMED, D);
  assert.deepEqual(t.amount, { start: 60, cap: 0, since: D, inc: 0, every: 0 });
  assert.deepEqual(t.weight, { start: 0, cap: 0, since: D, inc: 0, every: 0 });
  assert.deepEqual(t.show, { every: 0, since: D });
  assert.equal(t.alt, null);
  assert.equal(t.flags, 0);
  assert.equal(M.newStep(M.STEP.MANUAL, D).amount.start, 0);
  assert.deepEqual([M.MAX_STEPS, M.POOL_MAX, M.EVERY_MAX], [20, 240, 30]);
  assert.deepEqual(M.SFLAG, { QTY: 1, WEIGHT: 2 });
});

test('validate checks step-ups, rhythms and alternatives', function () {
  var rt = M.newItem(M.KIND.ROUTINE, 2, D);
  rt.name = 'Workout';
  var c = M.newStep(M.STEP.MANUAL, D);
  c.name = 'Curls'; c.flags = M.SFLAG.QTY | M.SFLAG.WEIGHT;
  c.amount.start = 8; c.weight.start = 100;
  rt.steps = [c];
  assert.deepEqual(M.validate(rt), []);
  function has(msg) { assert.ok(M.validate(rt).indexOf(msg) >= 0, msg + ' in ' + JSON.stringify(M.validate(rt))); }
  c.amount.inc = -1; c.amount.every = 7; has('Step 1: the increase must be above zero.');
  c.amount.inc = 1; c.amount.every = 0; has('Step 1: step up every 1 day or more.');
  c.amount.every = 7; c.weight.cap = 50; has('Step 1: stop at is below today\'s value.');
  c.weight.cap = 0; c.show.every = 31; has('Step 1: every 2 to 30 days.');
  c.show.every = 1; has('Step 1: every 2 to 30 days.');
  c.show.every = 2; c.alt = M.newStep(M.STEP.MANUAL, D); delete c.alt.alt; delete c.alt.show;
  has('Step 1\'s alternative needs a name.');
  c.alt.name = 'Shoulder press';
  assert.deepEqual(M.validate(rt), []);
  rt.repeat = { every: 31, since: D }; has('Repeat every 2 to 30 days.');
  rt.repeat.every = 3; assert.deepEqual(M.validate(rt), []);
});

test('the pool holds 240 steps across the schedule, alternatives included', function () {
  function routine(n, alts) {
    var it = M.newItem(M.KIND.ROUTINE, 1, D);
    for (var i = 0; i < n; i++) {
      var s = M.newStep(M.STEP.MANUAL, D);
      if (i < alts) s.alt = M.newStep(M.STEP.MANUAL, D);
      it.steps.push(s);
    }
    return it;
  }
  var items = [];
  for (var i = 0; i < 6; i++) items.push(routine(20, 20));  // 240
  assert.deepEqual(M.scheduleErrors(items), []);
  items.push(routine(1, 0));
  assert.deepEqual(M.scheduleErrors(items), ['At most 240 steps across all routines and tasks, alternatives included.']);
});

test('warnings name steps whose rhythm never meets the routine', function () {
  var rt = M.newItem(M.KIND.ROUTINE, 2, D);
  rt.days = 0x3E;  // Mon-Fri
  var s = M.newStep(M.STEP.MANUAL, D);
  s.name = 'Red light';
  rt.steps = [M.newStep(M.STEP.MANUAL, D), s];
  s.show = { every: 7, since: D + 1 };  // Saturdays
  assert.deepEqual(M.warnings(rt), ['Step 2 never falls on a day this routine runs.']);
  s.show = { every: 14, since: D };  // Fridays
  assert.deepEqual(M.warnings(rt), []);
  s.show = { every: 2, since: D + 1 };
  assert.deepEqual(M.warnings(rt), []);
  rt.repeat = { every: 4, since: D };
  assert.deepEqual(M.warnings(rt), ['Step 2 never falls on a day this routine runs.']);
  s.show.since = D + 2;
  assert.deepEqual(M.warnings(rt), []);
});

test('upgrade turns a 1.4.1 item into the 1.5.0 shape, once', function () {
  var old = { id: 1, kind: 0, days: 0x3E, name: 'Morning', steps: [
    { type: 1, dur: 90, num: { step: 0, min: 0, max: 0, decimals: 0, unit: '' }, name: 'Plank' }] };
  var up = M.upgrade(old);
  assert.equal(up.steps[0].amount.start, 90);
  assert.ok(!('dur' in up.steps[0]));
  assert.deepEqual(up.repeat, { every: 0, since: 0 });
  assert.deepEqual(up.steps[0].show, { every: 0, since: 0 });
  assert.equal(up.steps[0].alt, null);
  assert.deepEqual(M.upgrade(up), up);
  assert.equal(old.steps[0].dur, 90);  // the stored item is left alone
});

test('a weight joins the quantity logged with it', function () {
  var out = M.pairWeights([{ t: 100, item: 5, step: 0, v: 18, dec: 0, unit: '' },
    { t: 101, item: 5, step: 0x40, v: 125, dec: 1, unit: 'kg' }]);
  assert.equal(out.length, 1);
  assert.equal(out[0].text, '18 × 12.5 kg');
  assert.equal(out[0].weight.v, 125);  // History's headline shows the weight beside the count
  out = M.pairWeights([{ t: 100, item: 5, step: 0, v: 18, dec: 0, unit: '' },
    { t: 110, item: 5, step: 0x40, v: 125, dec: 1, unit: 'kg' }, { t: 120, item: 3, step: 255, v: 12, dec: 0, unit: 'reps' }]);
  assert.deepEqual(out.map(function (e) { return e.text; }), ['18', '12.5 kg', '12 reps']);
});


test('a fixed value that starts to step up counts a full period from today', function () {
  var r = M.rebase({ start: 20, cap: 0, since: D - 100, inc: 0, every: 0 }, 1, 7, D);
  assert.equal(M.valueAt(r, D), 20);
  assert.equal(M.valueAt(r, D + 6), 20);
  assert.equal(M.valueAt(r, D + 7), 21);
});

test('an every-N-days item needs no weekdays', function () {
  var t = M.newItem(M.KIND.TASK, 4, D);
  t.name = 'Water the plants'; t.flags = 0; t.days = 0;
  assert.ok(M.validate(t).indexOf('Pick at least one day.') >= 0);
  t.repeat = { every: 3, since: D };
  assert.deepEqual(M.validate(t), []);
});

test('a step-up left behind on a switched-off field never reaches the watch', function () {
  var it = M.newItem(M.KIND.ROUTINE, 1, D), s = M.newStep(M.STEP.MANUAL, D);
  it.name = 'Arms'; s.name = 'Curls';
  s.amount = { start: 8, cap: 0, since: D, inc: 1, every: 0 };  // an Increase, Every cleared, then Quantity off
  it.steps = [s];
  var c = M.clean(it);
  assert.deepEqual(M.validate(c), []);
  var r = c.steps[0].amount;
  assert.ok(!r.inc || r.every, 'the watch refuses an increase with no period');  // cc_codec.c rramp
});

test('an item that repeats every few days says so, not its leftover weekdays', function () {
  var r = M.newItem(M.KIND.ROUTINE, 1, D);
  r.repeat = { every: 3, since: D };
  assert.equal(M.whenText(r), 'every 3 days');
  assert.ok(M.summary(r).indexOf('every 3 days') === 0);
  var t = M.newItem(M.KIND.TASK, 2, D);  // anytime by default
  t.repeat = { every: 2, since: D };
  assert.equal(M.whenText(t), 'anytime, every 2 days');
  assert.equal(M.whenText(M.newItem(M.KIND.TASK, 3, D)), 'anytime');
  assert.equal(M.whenText(M.newItem(M.KIND.ROUTINE, 4, D)), 'Mon to Fri');
});

test('History keeps exercises apart when a routine\'s steps move', function () {
  var logs = [  // oldest first, as the phone keeps them
    { t: 100, item: 5, step: 2, v: 8, dec: 0, unit: '', label: 'Arms · Shoulder press' },
    { t: 200, item: 5, step: 2, v: 10, dec: 0, unit: '', label: 'Arms · Rows' },  // index 2 after a step was added
    { t: 201, item: 5, step: 0x42, v: 125, dec: 1, unit: 'kg', label: 'Arms · Rows' }];
  var s = M.series(logs);
  assert.deepEqual(s.map(function (g) { return g[0].label; }), ['Arms · Rows', 'Arms · Shoulder press']);
  assert.equal(M.pairWeights(s[0])[0].text, '10 × 12.5 kg');
  assert.equal(s[1].length, 1);
});

test('raising Stop at keeps today\'s value', function () {
  var held = { start: 20, cap: 25, since: D - 70, inc: 1, every: 7 };  // 30 uncapped, held at 25
  var r = M.setCap(held, 40, D);
  assert.equal(M.valueAt(r, D), 25);
  assert.equal(M.valueAt(r, D + 7), 26);
  var climbing = { start: 20, cap: 0, since: D - 14, inc: 1, every: 7 };  // 22 today
  r = M.setCap(climbing, 40, D);
  assert.equal(M.valueAt(r, D), 22);
  assert.equal(M.valueAt(r, D + 7), 23);
  assert.equal(r.cap, 40);
});

test('Stop at below today\'s value is refused, and one reached on its own is not', function () {
  var it = M.newItem(M.KIND.ROUTINE, 1, D), s = M.newStep(M.STEP.MANUAL, D);
  it.name = 'Arms'; s.name = 'Pushups'; s.flags = M.SFLAG.QTY; it.steps = [s];
  s.amount = M.setCap({ start: 20, cap: 0, since: D - 70, inc: 1, every: 7 }, 25, D);  // typed while at 30
  assert.ok(M.validate(it).some(function (m) { return /below today/.test(m); }));
  s.amount = { start: 20, cap: 25, since: D - 70, inc: 1, every: 7 };  // climbed to its Stop at since it was set
  assert.deepEqual(M.validate(it), []);
});

test('a typed value with more decimals than its number shows raises the decimals instead of losing the half', function () {
  assert.equal(M.decimalsFor('2.5'), 1);
  assert.equal(M.decimalsFor('2.50'), 1);
  assert.equal(M.decimalsFor('10'), 0);
  assert.equal(M.decimalsFor('10.00'), 0);
  assert.equal(M.decimalsFor('0.125'), 2);  // the planner offers 0 to 2
  var spec = { step: 1, min: 0, max: 500, decimals: 0, unit: 'kg' }, w = { start: 10, cap: 25, since: 5, inc: 2, every: 7 };
  M.setDecimals(spec, w, 1);
  assert.deepEqual(spec, { step: 10, min: 0, max: 5000, decimals: 1, unit: 'kg' });
  assert.deepEqual(w, { start: 100, cap: 250, since: 5, inc: 20, every: 7 });
  assert.equal(M.toScaled('2.5', spec.decimals), 25);
});

test('text that is not a number never changes the decimals', function () {
  assert.equal(M.decimalsFor('1..5'), 0);
  assert.equal(M.decimalsFor('abc.5'), 0);
  assert.equal(M.decimalsFor('.5'), 1);
});

// Step outcomes (0x80 | step: 1 done, 0 skipped) and a run's end (0xBF done, 0xBE skipped, 0xBD not finished:
// steps << 25 | done << 20 | seconds).
var RUN_LOGS = [
  { t: 1000, item: 5, step: 0x80, v: 1, label: 'Gym', stepName: 'Pushups' },
  { t: 1000, item: 5, step: 0, v: 22, label: 'Gym · Pushups', unit: '', dec: 0 },
  { t: 1100, item: 5, step: 0x81, v: 0, label: 'Gym', stepName: 'Plank' },
  { t: 1200, item: 5, step: 0x82, v: 1, label: 'Gym', stepName: 'Stretch' },
  { t: 3040, item: 5, step: 0xBF, v: 3 << 25 | 2 << 20 | 2040, label: 'Gym' },
  { t: 4000, item: 6, step: 0x80, v: 0, label: 'Morning', stepName: 'Water' },
  { t: 4001, item: 6, step: 0xBE, v: 1 << 25 | 30, label: 'Morning' },
  { t: 5000, item: 5, step: 0x80, v: 1, label: 'Gym', stepName: 'Pushups' }
];

test('History groups step outcomes into routine runs, newest first', function () {
  var r = M.runs(RUN_LOGS, 0);
  assert.equal(r.length, 3);
  assert.deepEqual([r[0].name, r[0].result, r[0].done, r[0].of], ['Gym', 'open', 1, 0]);
  assert.deepEqual([r[1].name, r[1].result, r[1].done, r[1].of, r[1].secs], ['Morning', 'skipped', 0, 1, 30]);
  assert.deepEqual([r[2].name, r[2].result, r[2].done, r[2].of, r[2].secs, r[2].t], ['Gym', 'done', 2, 3, 2040, 3040]);
  assert.deepEqual(r[2].skipped, ['Plank']);
  assert.equal(M.runText(r[2]), '2 of 3 · 34 min');
  assert.equal(M.runText(r[1]), 'skipped · 0 of 1');
  assert.equal(M.runText(r[0]), '1 done · not finished');
  assert.equal(M.runText({ result: 'done', done: 4, of: 4, secs: 0 }), '4 of 4');
});

test('a run left unfinished, or never ended before another day, stands on its own', function () {
  var DAY = 86400, logs = [
    { t: 1000, item: 5, step: 0x80, v: 1, label: 'Gym', stepName: 'Pushups' },
    { t: DAY + 1000, item: 5, step: 0x80, v: 1, label: 'Gym', stepName: 'Pushups' },
    { t: DAY + 1100, item: 5, step: 0x81, v: 1, label: 'Gym', stepName: 'Plank' },
    { t: DAY + 1200, item: 5, step: 0xBF, v: 2 << 25 | 2 << 20 | 200, label: 'Gym' },
    { t: DAY + 2000, item: 5, step: 0x80, v: 1, label: 'Gym', stepName: 'Pushups' },
    { t: DAY + 2100, item: 5, step: 0xBD, v: 2 << 25 | 1 << 20 | 100, label: 'Gym' }
  ];
  var r = M.runs(logs, 0);
  assert.equal(r.length, 3);
  assert.deepEqual([r[0].result, r[0].done, r[0].of], ['left', 1, 2]);
  assert.equal(M.runText(r[0]), '1 of 2 \u00b7 not finished');
  assert.deepEqual([r[1].result, r[1].done, r[1].of], ['done', 2, 2]);
  assert.deepEqual([r[2].result, r[2].done, r[2].t], ['open', 1, 1000]);
  assert.equal(M.csvRuns(logs, 0).split('\n')[3], '1970-01-02,00:35,Gym,not finished,1,2,2,');
  // The end's count stands even when the page kept none of the run's outcomes.
  var cut = [{ t: 3040, item: 5, step: 0xBF, v: 3 << 25 | 3 << 20 | 2040, label: 'Gym' }];
  assert.equal(M.runText(M.runs(cut, 0)[0]), '3 of 3 \u00b7 34 min');
});

test('outcomes stay out of the numbers', function () {
  var s = M.series(RUN_LOGS);
  assert.equal(s.length, 1);
  assert.equal(s[0][0].label, 'Gym · Pushups');
  assert.equal(M.csvNumbers(RUN_LOGS, 0).split('\n').length, 2);
});

test('routines export as CSV, one row a run', function () {
  var rows = M.csvRuns(RUN_LOGS, 0).split('\n');
  assert.equal(rows[0], 'date,time,routine,result,done,steps,minutes,skipped');
  assert.equal(rows.length, 4);
  assert.equal(rows[1], '1970-01-01,00:50,Gym,done,2,3,34,Plank');
  assert.equal(rows[2], '1970-01-01,01:06,Morning,skipped,0,1,1,Water');
  assert.equal(rows[3], '1970-01-01,01:23,Gym,not finished,1,,,');
});
