// One sync with the watch, driven from the phone (protocol table in docs/dev.md):
// HELLO, the watch's uploads, ACK, then the schedule and note edits if the watch needs them.
var codec = require('./codec.js');

var CMD = { HELLO: 1, UP: 2, UP_END: 3, ACK: 4, SCHED: 5, SCHED_END: 6, OPS: 7, DONE: 8, ASK: 9 };
var SILENCE_MS = 12000, BUSY_RETRY_MS = 30000;
var RESEND_MS = [500, 2000, 5000];  // a failed send tries again; the watch can be busy for ~4 s after an update

function u32(w, v) { w.push(v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255); }

// send(dict, ok, fail) wraps Pebble.sendAppMessage; timers has set(fn, ms), clear(id), now() in UTC seconds.
function Session(store, send, timers, log) {
  this.store = store;
  this.send = send;
  this.timers = timers;
  this.log = log || function () {};
  this.state = 'idle';
  this.queue = [];
  this.sending = false;
  this.again = false;
  this.timer = null;
  this.gen = 0;        // bumped by end(): callbacks from an ended sync do nothing
  this.resend = null;
}

Session.CMD = CMD;

Session.prototype.post = function (msg) {
  this.queue.push(msg);
  this.pump();
};

Session.prototype.pump = function () {
  if (this.sending || !this.queue.length) return;
  var self = this, msg = this.queue.shift(), tries = 0, gen = this.gen;
  this.sending = true;
  function go() {
    self.resend = null;
    self.send(msg, function () {
      if (gen !== self.gen) return;
      self.sending = false;
      self.alive();
      self.pump();
    }, function () {
      if (gen !== self.gen) return;
      if (tries < RESEND_MS.length) {
        self.alive();  // a refusal is an answer: the silence timer starts again
        self.resend = self.timers.set(go, RESEND_MS[tries++]);
        return;
      }
      self.sending = false;
      self.end('send failed');
    });
  }
  go();
};

// Any traffic keeps the sync alive; silence ends it.
Session.prototype.alive = function () {
  var self = this;
  if (this.timer) this.timers.clear(this.timer);
  this.timer = this.state === 'idle' ? null : this.timers.set(function () { self.end('watch went quiet'); }, SILENCE_MS);
};

Session.prototype.end = function (why) {
  if (why) this.log('sync: ' + why);
  this.state = 'idle';
  this.queue = [];
  this.gen++;
  this.sending = false;
  if (this.resend) { this.timers.clear(this.resend); this.resend = null; }
  this.alive();
  if (this.again) { this.again = false; this.hello(); }
};

Session.prototype.hello = function () {
  if (this.state !== 'idle') { this.again = true; return; }
  var c = this.store.cursors(), d = [];
  u32(d, this.store.meta.phoneId); u32(d, this.store.sched.rev); u32(d, c.logUtc); u32(d, c.histDay);
  this.state = 'up';
  this.up = { items: [], steps: [], settings: null, logs: [], hist: [], heads: [], parts: [], tombs: [] };
  this.post({ CMD: CMD.HELLO, DATA: d });
  this.alive();
};

Session.prototype.onMessage = function (p) {
  if (p.CMD === undefined) return;
  if (p.CMD === CMD.ASK) { this.log('sync: the watch asked'); this.hello(); return; }  // it has something new
  this.alive();
  try {
    if (p.CMD === CMD.UP && this.state === 'up') this.collect(p.DATA || []);
    else if (p.CMD === CMD.UP_END && this.state === 'up') this.upEnd(p.REV || 0);
    else if (p.CMD === CMD.DONE) this.done(p.ARG);
  } catch (e) {
    this.end('bad message: ' + e.message);
  }
};

Session.prototype.collect = function (data) {
  var u = this.up, frames = codec.framesRead(data);
  for (var i = 0; i < frames.length; i++) {
    var f = frames[i], T = codec.T;
    if (f.type === T.ITEM) u.items.push(codec.unpackItem(f.rec));
    else if (f.type === T.STEP) u.steps.push(codec.unpackStepAt(f.rec));
    else if (f.type === T.SETTINGS) u.settings = codec.unpackSettingsAt(f.rec);
    else if (f.type === T.LOG) u.logs = u.logs.concat(codec.unpackLogs(f.rec));
    else if (f.type === T.HIST) u.hist.push(codec.unpackHistory(f.rec));
    else if (f.type === T.NOTE_HEAD) u.heads.push(codec.unpackNoteHead(f.rec));
    else if (f.type === T.NOTE_PART) u.parts.push(codec.unpackNotePart(f.rec));
    else if (f.type === T.TOMBS) u.tombs = u.tombs.concat(codec.unpackTombs(f.rec));
  }
};

Session.prototype.upEnd = function (watchRev) {
  var st = this.store, u = this.up, adopted = 0, i;
  if (st.sched.rev === 0) {
    st.adopt(codec.withSteps(u.items, u.steps), u.settings && u.settings.settings, u.settings ? u.settings.utc : 0);
    adopted = st.sched.rev;
    watchRev = adopted;
    this.log('sync: adopted ' + u.items.length + ' items rev ' + adopted);
  } else if (u.settings && st.takeSettings(u.settings.settings, u.settings.utc)) {
    this.log('sync: took the watch settings');
  }
  st.addLogs(u.logs);
  for (i = 0; i < u.hist.length; i++) st.putDay(u.hist[i].day, u.hist[i].out);
  var ids = [];
  for (i = 0; i < u.heads.length; i++) {
    var n = codec.noteFinish(u.heads[i], u.parts);
    if (!n) continue;  // a part is missing: the watch sends it again next time
    st.putNote(n);
    u32(ids, n.id);
  }
  st.dropNotes(u.tombs);
  st.meta.lastSync = this.timers.now();
  st.save('meta');
  this.log('sync: up ' + u.logs.length + ' logs ' + u.hist.length + ' days ' + ids.length / 4 + ' notes ' +
    u.tombs.length + ' tombs, watch rev ' + watchRev);
  var ack = { CMD: CMD.ACK, ARG: u.tombs.length, REV: adopted };
  if (ids.length) ack.DATA = ids;  // the watch reads a missing DATA as no notes; an empty byte array may not survive the phone app
  this.post(ack);
  this.up = null;
  if (st.sched.rev > 0 && st.sched.rev !== watchRev) this.sendSchedule();
  else this.sendOps();
};

Session.prototype.sendSchedule = function () {
  var s = this.store.sched, list = [], T = codec.T;
  for (var i = 0; i < s.items.length; i++) {
    var it = s.items[i];
    list.push({ type: T.ITEM, rec: codec.packItem(it) });
    codec.stepRecords(it).forEach(function (rec) { list.push({ type: T.STEP, rec: rec }); });
  }
  list.push({ type: T.SETTINGS, rec: codec.packSettingsAt(s.settings, s.settingsUtc) });
  var msgs = codec.framesPut(list);
  this.state = 'sched';
  for (var m = 0; m < msgs.length; m++) this.post({ CMD: CMD.SCHED, DATA: msgs[m], ARG: m === 0 ? 1 : 0 });
  this.post({ CMD: CMD.SCHED_END, REV: s.rev, ARG: s.items.length });
  this.log('sync: sending schedule rev ' + s.rev + ', ' + s.items.length + ' items in ' + msgs.length + ' messages');
};

Session.prototype.sendOps = function () {
  var ops = this.store.ops;
  if (!ops.length) { this.end(); return; }
  var list = ops.map(function (o) {
    return o.op === 'rename' ? { type: codec.T.RENAME, rec: codec.packRename(o.id, o.name) }
                             : { type: codec.T.DELETE, rec: codec.packDelete(o.id) };
  });
  var first = codec.framesPut(list)[0];
  this.opsSent = ops.slice(0, codec.framesRead(first).length);  // the rest go in the next message
  this.state = 'ops';
  this.post({ CMD: CMD.OPS, DATA: first });
};

Session.prototype.done = function (arg) {
  var self = this;
  if (this.state === 'sched') {
    if (arg === 1) { this.log('sync: watch applied the schedule'); this.sendOps(); return; }
    if (arg === 0) {
      this.end('watch busy, trying again in 30 s');
      this.timers.set(function () { self.hello(); }, BUSY_RETRY_MS);
      return;
    }
    this.end('watch refused the schedule');
  } else if (this.state === 'ops') {
    if (arg === 1) {
      this.store.dropOps(this.opsSent);
      this.sendOps();
      return;
    }
    this.end('watch refused the note edits');
  }
};

module.exports = Session;
