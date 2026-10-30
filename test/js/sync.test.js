var test = require('node:test');
var assert = require('node:assert/strict');
var Store = require('../../src/pkjs/store.js');
var Session = require('../../src/pkjs/sync.js');
var codec = require('../../src/pkjs/codec.js');
var H = require('./helpers.js');
var V = require('../build/vectors.json');

var C = Session.CMD;

function fromHex(h) {
  var out = [];
  for (var i = 0; i < h.length; i += 2) out.push(parseInt(h.substr(i, 2), 16));
  return out;
}

function FakeTimers() { this.t = 0; this.list = []; this.seq = 0; }
FakeTimers.prototype.set = function (fn, ms) { var id = ++this.seq; this.list.push({ id: id, at: this.t + ms, fn: fn }); return id; };
FakeTimers.prototype.clear = function (id) { this.list = this.list.filter(function (x) { return x.id !== id; }); };
FakeTimers.prototype.now = function () { return 1761200000 + Math.floor(this.t / 1000); };
FakeTimers.prototype.advance = function (ms) {
  this.t += ms;
  var due = this.list.filter(function (x) { return x.at <= this.t; }, this);
  this.list = this.list.filter(function (x) { return x.at > this.t; }, this);
  due.forEach(function (x) { x.fn(); });
};

function rig(store) {
  var sent = [], timers = new FakeTimers();
  var s = new Session(store || new Store(new H.MemStorage()), function (msg, ok) { sent.push(msg); ok(); }, timers,
    function () {});
  return { s: s, sent: sent, timers: timers, store: s.store };
}

function u32(b, i) { return (b[i] | b[i + 1] << 8 | b[i + 2] << 16 | b[i + 3] << 24) >>> 0; }

// Everything the watch would upload on its first sync, as UP messages.
function firstUpload() {
  var frames = [{ type: codec.T.ITEM, rec: fromHex(V.items[0].hex) }];
  V.steps.forEach(function (v) { frames.push({ type: codec.T.STEP, rec: fromHex(v.hex) }); });
  frames.push({ type: codec.T.SETTINGS, rec: fromHex(V.settings.hex) });
  frames.push({ type: codec.T.LOG, rec: fromHex(V.logs.hex) });
  frames.push({ type: codec.T.HIST, rec: fromHex(V.history.hex) });
  frames.push({ type: codec.T.NOTE_HEAD, rec: fromHex(V.note.records[0]) });
  V.note.records.slice(1).forEach(function (h) { frames.push({ type: codec.T.NOTE_PART, rec: fromHex(h) }); });
  frames.push({ type: codec.T.TOMBS, rec: codec.packTombs([1792800001, 4000000000, 5]) });  // not the note above
  return codec.framesPut(frames);
}

test('hello sends the phone id, rev and both cursors', function () {
  var r = rig();
  r.s.hello();
  assert.equal(r.sent.length, 1);
  var m = r.sent[0];
  assert.equal(m.CMD, C.HELLO);
  assert.equal(m.DATA.length, 16);
  assert.equal(u32(m.DATA, 0), r.store.meta.phoneId);
  assert.equal(u32(m.DATA, 4), 0);
  assert.equal(u32(m.DATA, 8), 0);
  assert.equal(u32(m.DATA, 12), 0xFFFFFFFF);  // hist day -1: every day
  assert.equal(r.s.state, 'up');
});

test('an empty phone adopts the watch schedule and acks notes and tombstones', function () {
  var r = rig();
  r.s.hello();
  firstUpload().forEach(function (d) { r.s.onMessage({ CMD: C.UP, DATA: d }); });
  r.s.onMessage({ CMD: C.UP_END, REV: 0, ARG: 1 });
  assert.equal(r.store.sched.rev, 1);
  assert.equal(r.store.sched.items.length, 1);
  assert.equal(r.store.sched.items[0].steps.length, 4);
  assert.equal(r.store.sched.items[0].steps[2].name, 'Weight');
  assert.equal(r.store.sched.settings.focusWork, 30);
  assert.equal(r.store.logs.length, 3);
  assert.equal(r.store.hist.length, 1);
  assert.equal(r.store.notes.length, 1);
  assert.equal(r.store.notes[0].text, V.note.json.text);
  var ack = r.sent[1];
  assert.equal(ack.CMD, C.ACK);
  assert.deepEqual(ack.DATA, codec.packDelete(V.note.json.id));  // one u32 id
  assert.equal(ack.ARG, 3);
  assert.equal(ack.REV, 1);
  assert.equal(r.sent.length, 2);  // the watch already has rev 1: nothing to push
  assert.equal(r.s.state, 'idle');
});

test('a note missing a part is not acked', function () {
  var r = rig();
  r.s.hello();
  r.s.onMessage({ CMD: C.UP, DATA: codec.framesPut([{ type: codec.T.NOTE_HEAD, rec: fromHex(V.note.records[0]) }])[0] });
  r.s.onMessage({ CMD: C.UP_END, REV: 0, ARG: 1 });
  assert.equal(r.store.notes.length, 0);
  assert.equal(r.sent[1].CMD, C.ACK);
  assert.ok(!('DATA' in r.sent[1]));
});

function phoneAtRev3() {
  var st = new Store(new H.MemStorage());
  st.adopt(H.sampleItems(), H.clone(codec.SETTINGS_DEFAULT), 0);
  st.sched.rev = 3;
  st.save('sched');
  return st;
}

test('a newer phone schedule goes to the watch in messages of at most 900 bytes', function () {
  var st = phoneAtRev3();
  for (var i = 0; i < 20; i++) {
    var it = H.clone(H.sampleItems()[0]);
    it.id = 100 + i;
    st.sched.items.push(it);
  }
  var r = rig(st);
  r.s.hello();
  r.s.onMessage({ CMD: C.UP_END, REV: 1, ARG: 0 });
  assert.equal(r.sent[1].CMD, C.ACK);
  assert.equal(r.sent[1].REV, 0);
  assert.ok(!('DATA' in r.sent[1]), 'no notes: the ACK leaves DATA out rather than send an empty byte array');
  var sched = r.sent.filter(function (m) { return m.CMD === C.SCHED; });
  assert.ok(sched.length > 1);
  assert.equal(sched[0].ARG, 1);
  assert.equal(sched[1].ARG, 0);
  var items = [], steps = [], settings = 0;
  sched.forEach(function (m) {
    assert.ok(m.DATA.length <= 900);
    codec.framesRead(m.DATA).forEach(function (f) {
      if (f.type === codec.T.ITEM) items.push(codec.unpackItem(f.rec));
      if (f.type === codec.T.STEP) steps.push(codec.unpackStepAt(f.rec));
      if (f.type === codec.T.SETTINGS) settings++;
    });
  });
  assert.equal(items.length, 23);
  assert.equal(items[0].name, 'Morning routine');
  assert.equal(items[0].stepCount, 3);
  assert.equal(steps.length, 3 * 21);
  assert.equal(settings, 1);
  var end = r.sent[r.sent.length - 1];
  assert.equal(end.CMD, C.SCHED_END);
  assert.equal(end.REV, 3);
  assert.equal(end.ARG, 23);
  assert.equal(r.s.state, 'sched');
  r.s.onMessage({ CMD: C.DONE, ARG: 1 });
  assert.equal(r.s.state, 'idle');
});

test('a busy watch gets the schedule again 30 s later', function () {
  var r = rig(phoneAtRev3());
  r.s.hello();
  r.s.onMessage({ CMD: C.UP_END, REV: 1, ARG: 0 });
  r.s.onMessage({ CMD: C.DONE, ARG: 0 });
  assert.equal(r.s.state, 'idle');
  var before = r.sent.length;
  r.timers.advance(29000);
  assert.equal(r.sent.length, before);
  r.timers.advance(1000);
  assert.equal(r.sent[before].CMD, C.HELLO);
});

test('queued note ops follow the schedule and clear on DONE', function () {
  var st = phoneAtRev3();
  st.putNote({ id: 10, len: 1, link: 0, status: 0, reason: 0, flags: 0, name: '', text: 'a' });
  st.applyPlanner({ v: 1, nextId: 7, items: st.sched.items, settings: st.sched.settings,
    ops: [{ op: 'rename', id: 10, name: 'Garden ideas' }, { op: 'delete', id: 12 }] }, 1);
  var r = rig(st);
  r.s.hello();
  r.s.onMessage({ CMD: C.UP_END, REV: 3, ARG: 0 });
  var ops = r.sent[r.sent.length - 1];
  assert.equal(ops.CMD, C.OPS);
  var frames = codec.framesRead(ops.DATA);
  assert.deepEqual(frames.map(function (f) { return f.type; }), [codec.T.RENAME, codec.T.DELETE]);
  assert.deepEqual(frames[0].rec, codec.packRename(10, 'Garden ideas'));
  assert.equal(r.s.state, 'ops');
  r.store.applyPlanner({ v: 1, nextId: 7, items: st.sched.items, settings: st.sched.settings,
    ops: [{ op: 'rename', id: 10, name: 'Garden plans' }] }, 2);  // saved again while the first edits travel
  r.s.onMessage({ CMD: C.DONE, ARG: 1 });
  assert.deepEqual(r.store.ops, [{ op: 'rename', id: 10, name: 'Garden plans' }]);
  var again = r.sent[r.sent.length - 1];
  assert.equal(again.CMD, C.OPS);
  r.s.onMessage({ CMD: C.DONE, ARG: 1 });
  assert.deepEqual(r.store.ops, []);
  assert.equal(r.s.state, 'idle');
});

test('twelve seconds of silence gives up without clearing anything', function () {
  var st = phoneAtRev3();
  st.applyPlanner({ v: 1, nextId: 7, items: st.sched.items, settings: st.sched.settings,
    ops: [{ op: 'delete', id: 12 }] }, 1);
  var r = rig(st);
  r.s.hello();
  r.s.onMessage({ CMD: C.UP_END, REV: 3, ARG: 0 });
  assert.equal(r.s.state, 'ops');
  r.timers.advance(12000);
  assert.equal(r.s.state, 'idle');
  assert.equal(r.store.ops.length, 1);
});

test('a hello asked for mid-sync runs once the current one ends', function () {
  var r = rig();
  r.s.hello();
  r.s.hello();
  assert.equal(r.sent.length, 1);
  r.s.onMessage({ CMD: C.UP_END, REV: 0, ARG: 0 });
  var hellos = r.sent.filter(function (m) { return m.CMD === C.HELLO; });
  assert.equal(hellos.length, 2);
});

// Migrating a full 1.4.1 schedule keeps the watch busy for about 4 s after the update.
function slowWatch(busyMs) {
  var sent = [], tries = 0, timers = new FakeTimers();
  var s = new Session(new Store(new H.MemStorage()), function (msg, ok, fail) {
    tries++;
    if (timers.t < busyMs) { fail(); return; }
    sent.push(msg); ok();
  }, timers, function () {});
  return { s: s, sent: sent, timers: timers, tries: function () { return tries; } };
}

test('a watch busy for a few seconds after an update still gets the hello', function () {
  var w = slowWatch(4000);
  w.s.hello();
  for (var i = 0; i < 10; i++) w.timers.advance(1000);
  assert.equal(w.sent.length, 1);
  assert.equal(w.sent[0].CMD, C.HELLO);
  assert.equal(w.s.state, 'up');
});

test('a watch that never answers is given up on after a few tries', function () {
  var w = slowWatch(Infinity);
  w.s.hello();
  for (var i = 0; i < 20; i++) w.timers.advance(1000);
  assert.equal(w.s.state, 'idle');
  assert.equal(w.tries(), 4);
});

test('a sync that ends while a send waits to retry leaves the next hello alone', function () {
  var timers = new FakeTimers(), calls = [];
  var s = new Session(new Store(new H.MemStorage()), function (msg, ok, fail) { calls.push({ msg: msg, ok: ok, fail: fail }); },
    timers, function () {});
  s.hello();
  calls[0].fail();   // refused: the resend waits
  s.end('test');     // the sync ends meanwhile
  s.hello();         // a planner save starts the next one at once
  assert.equal(calls.length, 2);
  assert.equal(calls[1].msg.CMD, C.HELLO);
  timers.advance(10000);
  assert.equal(calls.length, 2);  // the old resend is dropped
  calls[1].ok();
  assert.equal(s.state, 'up');
});

test('sends that take long to fail are not cut short by the silence timer', function () {
  var timers = new FakeTimers(), tries = 0, sent = [];
  var s = new Session(new Store(new H.MemStorage()), function (msg, ok, fail) {
    if (++tries < 3) { timers.set(fail, 5000); return; }  // each refusal takes 5 s to arrive
    sent.push(msg); ok();
  }, timers, function () {});
  s.hello();
  for (var i = 0; i < 30; i++) timers.advance(500);
  assert.equal(sent.length, 1);
  assert.equal(s.state, 'up');
});
