var test = require('node:test');
var assert = require('node:assert/strict');
var Store = require('../../src/pkjs/store.js');
var codec = require('../../src/pkjs/codec.js');
var H = require('./helpers.js');

function adopted() {
  var st = new Store(new H.MemStorage());
  st.adopt(H.sampleItems(), H.clone(codec.SETTINGS_DEFAULT), 0);
  return st;
}

test('a fresh phone has no schedule and a phone id', function () {
  var st = new Store(new H.MemStorage());
  assert.equal(st.sched.rev, 0);
  assert.ok(st.meta.phoneId > 0 && st.meta.phoneId <= 0xFFFFFFFF);
  assert.deepEqual(st.cursors(), { logUtc: 0, histDay: -1 });
});

test('adopting the watch schedule starts at rev 1 with nextId past every id', function () {
  var st = adopted();
  assert.equal(st.sched.rev, 1);
  assert.equal(st.sched.nextId, 7);
  assert.equal(st.sched.items[0].steps.length, 3);
});

test('the same log chunk twice adds nothing; two entries in one second are both kept', function () {
  var st = adopted();
  var chunk = [{ t: 100, item: 1, step: 2, v: 824 }, { t: 100, item: 2, step: 255, v: 12 }];
  assert.equal(st.addLogs(chunk), 2);
  assert.equal(st.addLogs(H.clone(chunk)), 0);
  assert.equal(st.addLogs([{ t: 100, item: 1, step: 2, v: 824 }, { t: 160, item: 1, step: 2, v: 821 }]), 1);
  assert.equal(st.logs.length, 3);
  assert.equal(st.logs[0].label, 'Morning routine · Weight');
  assert.equal(st.logs[0].unit, 'kg');
  assert.equal(st.logs[0].dec, 1);
  assert.equal(st.logs[1].label, 'Drink water');
  assert.deepEqual(st.cursors(), { logUtc: 160, histDay: -1 });
});

test('a log for an item the phone does not know still lands, labelled by id', function () {
  var st = adopted();
  st.addLogs([{ t: 5, item: 99, step: 255, v: 3 }]);
  assert.equal(st.logs[0].label, 'Item 99');
});

test('a second record for one day replaces the first; only 365 days are kept', function () {
  var st = adopted();
  st.putDay(20000, [{ id: 1, status: 2, count: 0, missed: 0 }]);
  st.putDay(20000, [{ id: 1, status: 3, count: 0, missed: 0 }]);
  assert.equal(st.hist.length, 1);
  assert.equal(st.hist[0].out[0].status, 3);
  assert.equal(st.hist[0].out[0].name, 'Morning routine');
  for (var d = 20001; d <= 20365; d++) st.putDay(d, [{ id: 2, status: 0, count: 1, missed: 0 }]);
  assert.equal(st.hist.length, 365);
  assert.equal(st.hist[0].day, 20001);
  assert.deepEqual(st.cursors().histDay, 20365);
});

test('a note sent again after a watch rename takes the new name; tombstones delete', function () {
  var st = adopted();
  var n = { id: 1792800000, len: 12, link: 1, status: 0, reason: 0, flags: 0, name: '', text: 'Buy oat milk' };
  st.putNote(n);
  st.putNote(H.clone(n));
  assert.equal(st.notes.length, 1);
  assert.equal(st.notes[0].linkName, 'Morning routine');
  st.putNote({ id: 1792800000, len: 12, link: 1, status: 0, reason: 0, flags: 0, name: 'Shopping', text: 'Buy oat milk' });
  assert.equal(st.notes[0].name, 'Shopping');
  st.putNote({ id: 1792800005, len: 2, link: 0, status: 1, reason: 5, flags: 0, name: '', text: '' });
  st.dropNotes([1792800000]);
  assert.deepEqual(st.notes.map(function (x) { return x.id; }), [1792800005]);
});

test('planner saves: identical items keep the rev, an edit or a new item bumps it', function () {
  var st = adopted();
  var r = st.applyPlanner({ v: 1, nextId: 7, items: H.sampleItems(), settings: H.clone(codec.SETTINGS_DEFAULT), ops: [] }, 1000);
  assert.equal(r.changed, false);
  assert.equal(st.sched.rev, 1);
  var items = H.sampleItems();
  items[1].name = 'Water, big glass';
  st.applyPlanner({ v: 1, nextId: 7, items: items, settings: H.clone(codec.SETTINGS_DEFAULT), ops: [] }, 1000);
  assert.equal(st.sched.rev, 2);
  items = H.clone(items);
  items.push({ id: 7, kind: 1, priority: 0, flags: 0, days: 127, at: 540, until: 1020, every: 45, goal: 0, grace: 0,
    order: 3, eventDay: 0, ladder: [], num: H.NONE, name: 'Stand up', steps: [] });
  st.applyPlanner({ v: 1, nextId: 8, items: items, settings: H.clone(codec.SETTINGS_DEFAULT), ops: [] }, 1000);
  assert.equal(st.sched.rev, 3);
  assert.equal(st.sched.nextId, 8);
  assert.equal(st.sched.settingsUtc, 0);  // settings untouched
});

test('planner settings bump the rev and stamp the time; older watch settings are ignored', function () {
  var st = adopted();
  var s = H.clone(codec.SETTINGS_DEFAULT);
  s.focusWork = 30;
  st.applyPlanner({ v: 1, nextId: 7, items: H.sampleItems(), settings: s, ops: [] }, 5000);
  assert.equal(st.sched.rev, 2);
  assert.equal(st.sched.settingsUtc, 5000);
  var w = H.clone(codec.SETTINGS_DEFAULT);
  w.laterTask = 45;
  assert.equal(st.takeSettings(w, 4000), false);
  assert.equal(st.sched.settings.focusWork, 30);
  assert.equal(st.takeSettings(w, 6000), true);
  assert.equal(st.sched.settings.laterTask, 45);
});

test('note ops rename and delete locally and queue for the watch', function () {
  var st = adopted();
  st.putNote({ id: 10, len: 1, link: 0, status: 0, reason: 0, flags: 0, name: '', text: 'a' });
  st.putNote({ id: 11, len: 1, link: 0, status: 0, reason: 0, flags: 0, name: '', text: 'b' });
  st.applyPlanner({ v: 1, nextId: 7, items: H.sampleItems(), settings: H.clone(codec.SETTINGS_DEFAULT),
    ops: [{ op: 'rename', id: 10, name: 'First' }, { op: 'rename', id: 10, name: 'Garden ideas' }, { op: 'delete', id: 11 }] }, 1);
  assert.equal(st.notes.length, 1);
  assert.equal(st.notes[0].name, 'Garden ideas');
  assert.deepEqual(st.ops, [{ op: 'rename', id: 10, name: 'Garden ideas' }, { op: 'delete', id: 11 }]);
  // A note re-sent by the watch before the rename reached it keeps the planner's name.
  st.putNote({ id: 10, len: 1, link: 0, status: 0, reason: 0, flags: 0, name: '', text: 'a' });
  assert.equal(st.notes[0].name, 'Garden ideas');
  st.dropOps([st.ops[0]]);
  assert.deepEqual(st.ops, [{ op: 'delete', id: 11 }]);
});

test('everything survives a new Store over the same storage', function () {
  var mem = new H.MemStorage();
  var st = new Store(mem);
  st.adopt(H.sampleItems(), H.clone(codec.SETTINGS_DEFAULT), 0);
  st.addLogs([{ t: 100, item: 1, step: 2, v: 824 }]);
  st.putDay(20000, [{ id: 1, status: 2, count: 0, missed: 0 }]);
  st.putNote({ id: 10, len: 1, link: 0, status: 0, reason: 0, flags: 0, name: '', text: 'a' });
  var again = new Store(mem);
  assert.equal(again.meta.phoneId, st.meta.phoneId);
  assert.equal(again.sched.rev, 1);
  assert.equal(again.logs.length, 1);
  assert.equal(again.hist.length, 1);
  assert.equal(again.notes.length, 1);
});

test('plannerData carries what the page needs', function () {
  var st = adopted();
  var d = st.plannerData(1234);
  assert.equal(d.v, 1);
  assert.equal(d.nextId, 7);
  assert.equal(d.items.length, 3);
  assert.equal(d.now, 1234);
  assert.ok(Array.isArray(d.notes) && Array.isArray(d.logs) && Array.isArray(d.hist));
});

test('a note deleted in the planner stays deleted when the watch sends it again before the delete lands', function () {
  var st = adopted();
  st.putNote({ id: 10, len: 1, link: 0, status: 0, reason: 0, flags: 0, name: '', text: 'a' });
  st.applyPlanner({ v: 1, ops: [{ op: 'delete', id: 10 }] }, 1);
  assert.equal(st.notes.length, 0);
  // Renamed on the watch (or its ACK was lost), so the next upload carries it again.
  st.putNote({ id: 10, len: 1, link: 0, status: 0, reason: 0, flags: 0, name: 'Watch name', text: 'a' });
  assert.equal(st.notes.length, 0);
  assert.deepEqual(st.ops, [{ op: 'delete', id: 10 }]);
});

test('plannerData stays under its byte budget after years of use and keeps the newest of each', function () {
  var st = adopted();
  H.yearsOfUse(st, 2, 'Remember to call the garage about the brakes and ask whether they can fit it in on Thursday. ' +
    'The noise is getting worse on the hill, and the front left wheel squeals when it is wet, so mention that too. ' +
    'Ask for a price on the pads and the discs both, and whether they can do the MOT the same morning.');
  var d = st.plannerData(1, 200 * 1024);
  var bytes = codec.utf8Bytes(JSON.stringify([d.notes, d.logs, d.hist]));
  assert.ok(bytes <= 200 * 1024, 'notes, logs and days fit the budget: ' + bytes);
  assert.equal(d.hist.length, 60);
  assert.equal(d.hist[59].day, st.hist[st.hist.length - 1].day);
  assert.equal(d.more.days, 365 - 60);
  assert.ok(d.notes.length > 100 && d.more.notes === st.notes.length - d.notes.length);
  assert.equal(d.notes[d.notes.length - 1].id, st.notes[st.notes.length - 1].id);
  assert.ok(d.logs.length > 100 && d.more.logs === st.logs.length - d.logs.length);
  assert.equal(d.logs[d.logs.length - 1].t, st.logs[st.logs.length - 1].t);
});

test('a phone with little data sends all of it', function () {
  var st = adopted();
  H.yearsOfUse(st, 0.1, 'Short note.');
  var d = st.plannerData(1);
  assert.deepEqual(d.more, { notes: 0, logs: 0, days: 0 });
  assert.equal(d.notes.length, st.notes.length);
  assert.equal(d.logs.length, st.logs.length);
});

test('a 1.4.1 schedule loads in the 1.5.0 shape', function () {
  var mem = new H.MemStorage();
  var old = { rev: 3, nextId: 7, items: H.sampleItems(), settings: H.clone(codec.SETTINGS_DEFAULT), settingsUtc: 0 };
  mem.setItem('sched', JSON.stringify(old));
  var st = new Store(mem);
  assert.deepEqual(st.sched.items[0].repeat, { every: 0, since: 0 });
  assert.equal(st.sched.items[0].steps[1].amount.start, 60);
  assert.ok(!('dur' in st.sched.items[0].steps[1]));
  assert.equal(st.sched.rev, 3);
});

test('labels find alternatives and weights', function () {
  var st = adopted();
  var w = { id: 9, kind: 0, days: 127, at: 420, name: 'Workout', num: { step: 0, min: 0, max: 0, decimals: 0, unit: '' },
    steps: [
      { type: 0, flags: 3, name: 'Curls', num: { step: 0, min: 0, max: 0, decimals: 1, unit: 'kg' },
        alt: { type: 0, flags: 1, name: 'Shoulder press', num: { step: 0, min: 0, max: 0, decimals: 0, unit: '' } } },
      { type: 1, flags: 0, name: 'Plank', amount: { start: 60, cap: 0, since: 0, inc: 5, every: 7 } }] };
  st.adopt(st.sched.items.concat([w]), null, 0);
  assert.deepEqual(st.labelFor(9, 0), { label: 'Workout · Curls', unit: '', dec: 0 });
  assert.deepEqual(st.labelFor(9, 0x40), { label: 'Workout · Curls', unit: 'kg', dec: 1 });
  assert.deepEqual(st.labelFor(9, 2), { label: 'Workout · Shoulder press', unit: '', dec: 0 });
  assert.deepEqual(st.labelFor(9, 1), { label: 'Workout · Plank', unit: 's', dec: 0 });
  assert.deepEqual(st.labelFor(2, 255), { label: 'Drink water', unit: 'reps', dec: 0 });
  assert.deepEqual(st.labelFor(1, 2), { label: 'Morning routine · Weight', unit: 'kg', dec: 1 });
  // A step's outcome and a run's end carry the routine's name; an outcome also its step's.
  assert.deepEqual(st.labelFor(9, 0x82), { label: 'Workout', unit: '', dec: 0, stepName: 'Shoulder press' });
  assert.deepEqual(st.labelFor(9, 0xBF), { label: 'Workout', unit: '', dec: 0 });
  st.addLogs([{ t: 50, item: 9, step: 0x81, v: 0 }]);
  assert.equal(st.logs[st.logs.length - 1].stepName, 'Plank');
});
