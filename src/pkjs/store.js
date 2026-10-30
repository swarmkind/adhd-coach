// The phone's master copy: schedule, number logs, daily outcomes, voice notes and note edits
// waiting for the watch. Each part is one localStorage value, always written with setItem.
var codec = require('./codec.js');
var model = require('./planner_model.js');

var KEYS = ['meta', 'sched', 'logs', 'hist', 'notes', 'ops'];
var HIST_DAYS = 365;
var STEP_NONE = 255;  // a number logged on the item itself, not a routine step
var LOG_WEIGHT = 0x40;  // with a step index: the weight of that step
var PLANNER_DAYS = 60;  // the planner's history shows this many days
var PLANNER_BUDGET = 200 * 1024;  // bytes of notes, logs and days in the planner page

function copy(x) { return JSON.parse(JSON.stringify(x)); }

function Store(storage) {
  this.st = storage;
  this.load();
}

Store.prototype.read = function (key) {
  try { var v = this.st.getItem(key); return v ? JSON.parse(v) : null; } catch (e) { return null; }
};

Store.prototype.save = function (key) { this.st.setItem(key, JSON.stringify(this[key])); };
Store.prototype.saveAll = function () { for (var i = 0; i < KEYS.length; i++) this.save(KEYS[i]); };

Store.prototype.load = function () {
  this.meta = this.read('meta');
  if (!this.meta || !this.meta.phoneId) {
    this.meta = { phoneId: Math.floor(Math.random() * 0xFFFFFFFE) + 1, lastSync: 0 };
    this.save('meta');
  }
  this.sched = this.read('sched') || { rev: 0, nextId: 1, items: [], settings: copy(codec.SETTINGS_DEFAULT), settingsUtc: 0 };
  this.sched.items = this.sched.items.map(model.upgrade);  // 1.4.1 schedules open unchanged
  this.logs = this.read('logs') || [];
  this.hist = this.read('hist') || [];
  this.notes = this.read('notes') || [];
  this.ops = this.read('ops') || [];
};

// What the watch should send from: logs at or after logUtc, days after histDay.
Store.prototype.cursors = function () {
  var t = 0, d = -1, i;
  for (i = 0; i < this.logs.length; i++) if (this.logs[i].t > t) t = this.logs[i].t;
  for (i = 0; i < this.hist.length; i++) if (this.hist[i].day > d) d = this.hist[i].day;
  return { logUtc: t, histDay: d };
};

Store.prototype.item = function (id) {
  for (var i = 0; i < this.sched.items.length; i++) if (this.sched.items[i].id === id) return this.sched.items[i];
  return null;
};

function maxId(items) {
  var m = 0;
  for (var i = 0; i < items.length; i++) if (items[i].id > m) m = items[i].id;
  return m;
}

// The phone had no schedule: the watch's (sample or earlier) becomes the master copy at rev 1.
Store.prototype.adopt = function (items, settings, utc) {
  this.sched.items = copy(items).map(model.upgrade);
  this.sched.rev = 1;
  this.sched.nextId = Math.max(this.sched.nextId || 1, maxId(items) + 1);
  if (settings) { this.sched.settings = copy(settings); this.sched.settingsUtc = utc || 0; }
  this.save('sched');
};

Store.prototype.takeSettings = function (settings, utc) {
  if (!(utc > this.sched.settingsUtc)) return false;
  this.sched.settings = copy(settings);
  this.sched.settingsUtc = utc;
  this.save('sched');
  return true;
};

// Step `i` among an item's records: a main step, or past them the alternatives in their main steps' order.
function stepAt(steps, i) {
  if (i < steps.length) return steps[i];
  var rank = i - steps.length;
  for (var j = 0; j < steps.length; j++) if (steps[j].alt && rank-- === 0) return steps[j].alt;
  return null;
}

// A weight and a number show their own unit; a quantity has none; a timer's length is in seconds.
Store.prototype.labelFor = function (itemId, step) {
  var it = this.item(itemId);
  if (!it) return { label: 'Item ' + itemId, unit: '', dec: 0 };
  if (step !== STEP_NONE && (step & model.LOG.STEP)) {  // a routine step's outcome, or the run's end
    var o = { label: it.name, unit: '', dec: 0 }, done = stepAt(it.steps || [], step & 0x3F);
    if ((step & 0x3F) < 0x3D && done) o.stepName = done.name;  // 0x3D and up: a run's end
    return o;
  }
  var s = step !== STEP_NONE ? stepAt(it.steps || [], step & 0x3F) : null;
  if (!s) return { label: it.name, unit: it.num ? it.num.unit : '', dec: it.num ? it.num.decimals : 0 };
  var label = it.name + ' · ' + s.name;
  if ((step & LOG_WEIGHT) || s.type === model.STEP.NUMBER) return { label: label, unit: s.num.unit, dec: s.num.decimals };
  return { label: label, unit: s.type === model.STEP.TIMED ? 's' : '', dec: 0 };
};

function logKey(e) { return e.t + '|' + e.item + '|' + e.step + '|' + e.v; }

// Chunks are sent again until the phone has moved past them, so duplicates are expected here.
Store.prototype.addLogs = function (entries) {
  if (!entries.length) return 0;
  var from = entries[0].t, i;
  for (i = 1; i < entries.length; i++) if (entries[i].t < from) from = entries[i].t;
  var seen = {};
  for (i = 0; i < this.logs.length; i++) if (this.logs[i].t >= from) seen[logKey(this.logs[i])] = true;
  var added = 0;
  for (i = 0; i < entries.length; i++) {
    var e = entries[i], k = logKey(e);
    if (seen[k]) continue;
    seen[k] = true;
    var l = this.labelFor(e.item, e.step);
    var o = { t: e.t, item: e.item, step: e.step, v: e.v, label: l.label, unit: l.unit, dec: l.dec };
    if (l.stepName) o.stepName = l.stepName;
    this.logs.push(o);
    added++;
  }
  if (added) {
    this.logs.sort(function (a, b) { return a.t - b.t; });
    this.save('logs');
  }
  return added;
};

Store.prototype.putDay = function (day, outcomes) {
  var self = this;
  var out = outcomes.map(function (o) {
    var it = self.item(o.id);
    return { id: o.id, status: o.status, count: o.count, missed: o.missed, name: it ? it.name : 'Item ' + o.id,
      kind: it ? it.kind : -1 };
  });
  this.hist = this.hist.filter(function (h) { return h.day !== day; });
  this.hist.push({ day: day, out: out });
  this.hist.sort(function (a, b) { return a.day - b.day; });
  if (this.hist.length > HIST_DAYS) this.hist = this.hist.slice(this.hist.length - HIST_DAYS);
  this.save('hist');
};

Store.prototype.note = function (id) {
  for (var i = 0; i < this.notes.length; i++) if (this.notes[i].id === id) return this.notes[i];
  return null;
};

Store.prototype.pendingRename = function (id) {
  for (var i = this.ops.length - 1; i >= 0; i--) if (this.ops[i].id === id && this.ops[i].op === 'rename') return this.ops[i];
  return null;
};

Store.prototype.putNote = function (n) {
  for (var i = 0; i < this.ops.length; i++) if (this.ops[i].id === n.id && this.ops[i].op === 'delete') return;  // on its way out
  var link = n.link ? this.item(n.link) : null;
  var rec = { id: n.id, len: n.len, link: n.link, linkName: link ? link.name : '', status: n.status, reason: n.reason,
    name: n.name, text: n.text };
  var old = this.note(n.id), op = this.pendingRename(n.id);
  if (op) rec.name = op.name;  // the planner's rename hasn't reached the watch yet
  if (old) {
    if (!rec.text && old.text) rec.text = old.text;
    if (!rec.linkName) rec.linkName = old.linkName;
    for (var k in rec) old[k] = rec[k];
  } else {
    this.notes.push(rec);
    this.notes.sort(function (a, b) { return a.id - b.id; });
  }
  this.save('notes');
};

Store.prototype.dropNotes = function (ids) {
  if (!ids.length) return;
  var gone = {};
  for (var i = 0; i < ids.length; i++) gone[ids[i]] = true;
  this.notes = this.notes.filter(function (n) { return !gone[n.id]; });
  this.ops = this.ops.filter(function (o) { return !gone[o.id]; });
  this.save('notes');
  this.save('ops');
};

Store.prototype.queueOp = function (op) {
  var later = op.op === 'delete';
  this.ops = this.ops.filter(function (o) { return o.id !== op.id || (o.op === 'delete' && !later); });
  if (op.op === 'rename') {
    var n = this.note(op.id);
    if (!n) return;
    op = { op: 'rename', id: op.id, name: codec.utf8Cut(op.name, codec.NAME_MAX) };
    n.name = op.name;
  } else {
    this.notes = this.notes.filter(function (x) { return x.id !== op.id; });
    op = { op: 'delete', id: op.id };
  }
  this.ops.push(op);
};

// The watch applied these (by identity: an edit queued meanwhile replaces its op and stays).
Store.prototype.dropOps = function (sent) {
  this.ops = this.ops.filter(function (o) { return sent.indexOf(o) < 0; });
  this.save('ops');
};

function sameJSON(a, b) { return JSON.stringify(a) === JSON.stringify(b); }

// The planner closed with Save. `now` is UTC seconds.
Store.prototype.applyPlanner = function (res, now) {
  var changed = false;
  var items = res.items && copy(res.items).map(model.upgrade);
  if (items && !sameJSON(items, this.sched.items)) {
    this.sched.items = items;
    changed = true;
  }
  if (res.settings && !sameJSON(res.settings, this.sched.settings)) {
    this.sched.settings = copy(res.settings);
    this.sched.settingsUtc = now;
    changed = true;
  }
  if (changed) this.sched.rev++;
  this.sched.nextId = Math.max(this.sched.nextId, res.nextId || 0, maxId(this.sched.items) + 1);
  var ops = res.ops || [];
  for (var i = 0; i < ops.length; i++) this.queueOp(ops[i]);
  this.saveAll();
  return { changed: changed, ops: ops.length };
};

// The newest entries of `list` whose JSON fits in `budget` bytes, in stored order.
function newest(list, budget) {
  var n = 0, bytes = 0;
  for (var i = list.length - 1; i >= 0; i--) {
    var b = codec.utf8Bytes(JSON.stringify(list[i])) + 1;
    if (bytes + b > budget) break;
    bytes += b;
    n++;
  }
  return { list: list.slice(list.length - n), bytes: bytes };
}

// What the planner page shows. The page travels as one data: URL, so notes, logs and days are the
// newest that fit `budget` bytes of JSON (days: the 60 its history shows); `more` counts what stayed
// behind, which the page's exports fetch from here instead.
Store.prototype.plannerData = function (now, budget) {
  var hist = this.hist.slice(-PLANNER_DAYS);
  var left = (budget || PLANNER_BUDGET) - codec.utf8Bytes(JSON.stringify(hist));
  var notes = newest(this.notes, left * 3 / 4);
  var logs = newest(this.logs, left - notes.bytes);
  return { v: 1, nextId: this.sched.nextId, items: this.sched.items, settings: this.sched.settings, notes: notes.list,
    logs: logs.list, hist: hist, now: now, lastSync: this.meta.lastSync,
    more: { notes: this.notes.length - notes.list.length, logs: this.logs.length - logs.list.length,
      days: this.hist.length - hist.length } };
};

module.exports = Store;
