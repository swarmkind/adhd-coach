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
  for (var i = 0; i < 13; i++) rt.steps.push(M.newStep(M.STEP.MANUAL));
  rt.steps.forEach(function (s, j) { s.name = 'Step ' + j; });
  assert.ok(M.validate(rt).indexOf('At most 12 steps.') >= 0);
  rt.steps = [M.newStep(M.STEP.NUMBER)];
  rt.steps[0].name = 'Weight';
  rt.steps[0].num = { step: 1, min: 900, max: 300, decimals: 1, unit: 'kg' };
  assert.ok(M.validate(rt).indexOf('Step 1: the lowest value is above the highest.') >= 0);
  rt.steps = [M.newStep(M.STEP.TIMED)];
  rt.steps[0].name = 'Plank';
  rt.steps[0].dur = 0;
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
