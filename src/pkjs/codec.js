// The watch's record formats (src/c/core/cc_codec.c, cc_note.c, cc_sync.c), byte for byte.
// Little-endian; every record starts with a version byte. Bytes are plain arrays of numbers.
// ES5, so it runs in PebbleKit JS and in node tests alike.

var V1 = 1, V2 = 2;
var NAME_MAX = 31, UNIT_MAX = 5;  // bytes, the watch's char[32] and char[6] less the NUL
var NOTE_HEAD_TEXT = 200, NOTE_PART_TEXT = 240;
var DATA_MAX = 900;
var T = { ITEM: 1, STEP: 2, SETTINGS: 3, LOG: 4, HIST: 5, NOTE_HEAD: 6, NOTE_PART: 7, TOMBS: 8, RENAME: 9, DELETE: 10 };
var SETTINGS_DEFAULT = { laterTask: 30, laterReminder: 10, headsupImportant: 10, headsupCritical: 15, catchup: 10,
  eventTime: 540, laterEvent: 120, focusWork: 25, focusShort: 5, focusLong: 15, focusCycles: 4 };

function utf8Encode(s) {
  var bin;
  try { bin = unescape(encodeURIComponent(s)); } catch (e) { bin = String(s).replace(/[\ud800-\udfff]/g, '?'); }
  var out = [];
  for (var i = 0; i < bin.length; i++) out.push(bin.charCodeAt(i) & 255);
  return out;
}

function utf8Decode(bytes) {
  var bin = '';
  for (var i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  try { return decodeURIComponent(escape(bin)); } catch (e) { return bin; }
}

function cutBytes(b, max) {
  if (b.length <= max) return b;
  var cut = max;
  while (cut > 0 && (b[cut] & 0xC0) === 0x80) cut--;  // never split a character
  return b.slice(0, cut);
}

function utf8Cut(s, max) { return utf8Decode(cutBytes(utf8Encode(s || ''), max)); }
function utf8Bytes(s) { return utf8Encode(s || '').length; }

function Writer() { this.b = []; }
Writer.prototype.u8 = function (v) { this.b.push(v & 255); return this; };
Writer.prototype.u16 = function (v) { this.b.push(v & 255, (v >>> 8) & 255); return this; };
Writer.prototype.u32 = function (v) { this.u16(v & 0xFFFF); this.u16((v >>> 16) & 0xFFFF); return this; };
Writer.prototype.bytes = function (a) { for (var i = 0; i < a.length; i++) this.b.push(a[i] & 255); return this; };
Writer.prototype.str = function (s, max) { var a = cutBytes(utf8Encode(s || ''), max); this.u8(a.length); return this.bytes(a); };

function Reader(b) { this.b = b; this.pos = 0; }
Reader.prototype.need = function (n) { if (this.pos + n > this.b.length) throw new Error('short record'); };
Reader.prototype.u8 = function () { this.need(1); return this.b[this.pos++]; };
Reader.prototype.u16 = function () { var a = this.u8(); return a | (this.u8() << 8); };
Reader.prototype.u32 = function () { var a = this.u16(); return (a | (this.u16() << 16)) >>> 0; };
Reader.prototype.i8 = function () { var v = this.u8(); return v > 127 ? v - 256 : v; };
Reader.prototype.i16 = function () { return (this.u16() << 16) >> 16; };
Reader.prototype.i32 = function () { return this.u32() | 0; };
Reader.prototype.bytes = function (n) { this.need(n); var a = this.b.slice(this.pos, this.pos + n); this.pos += n; return a; };
Reader.prototype.str = function () { return utf8Decode(this.bytes(this.u8())); };
Reader.prototype.version = function (v) { if (this.u8() !== v) throw new Error('bad version'); };

function writeNum(w, n) {
  n = n || {};
  w.u16(n.step || 0).u32(n.min || 0).u32(n.max || 0).u8(n.decimals || 0).str(n.unit, UNIT_MAX);
}
function readNum(r) { return { step: r.i16(), min: r.i32(), max: r.i32(), decimals: r.u8(), unit: r.str() }; }

function packItem(it) {
  var w = new Writer(), ladder = it.ladder || [];
  w.u8(V1).u16(it.id).u8(it.kind).u8(it.priority || 0).u8(it.flags || 0).u8(it.days || 0);
  w.u16(it.at || 0).u16(it.until || 0).u16(it.every || 0);
  w.u8(it.goal || 0).u8(it.grace || 0).u8(it.order || 0).u8(it.steps ? it.steps.length : (it.stepCount || 0));
  w.u32(it.eventDay || 0).u8(ladder.length);
  for (var i = 0; i < 6; i++) w.u8(i < ladder.length ? ladder[i] : 0);
  writeNum(w, it.num);
  w.str(it.name, NAME_MAX);
  return w.b;
}

// The item's steps arrive in their own records: `stepCount` says how many to expect.
function unpackItem(b) {
  var r = new Reader(b);
  r.version(V1);
  var it = { id: r.u16(), kind: r.u8(), priority: r.u8(), flags: r.u8(), days: r.u8(), at: r.u16(), until: r.u16(),
    every: r.u16(), goal: r.u8(), grace: r.u8(), order: r.u8(), stepCount: r.u8(), eventDay: r.i32() };
  var n = r.u8(), ladder = [];
  for (var i = 0; i < 6; i++) { var v = r.i8(); if (i < n) ladder.push(v); }
  it.ladder = ladder;
  it.num = readNum(r);
  it.name = r.str();
  return it;
}

function packStepAt(item, index, s) {
  var w = new Writer();
  w.u16(item).u8(index).u8(V1).u8(s.type).u16(s.dur || 0);
  writeNum(w, s.num);
  w.str(s.name, NAME_MAX);
  return w.b;
}

function unpackStepAt(b) {
  var r = new Reader(b);
  var item = r.u16(), index = r.u8();
  r.version(V1);
  var s = { type: r.u8(), dur: r.u16() };
  s.num = readNum(r);
  s.name = r.str();
  return { item: item, index: index, step: s };
}

function packSettingsAt(s, utc) {
  var w = new Writer();
  w.u32(utc).u8(V2).u8(s.laterTask).u8(s.laterReminder).u8(s.headsupImportant).u8(s.headsupCritical).u8(s.catchup);
  w.u16(s.eventTime).u8(s.laterEvent).u8(s.focusWork).u8(s.focusShort).u8(s.focusLong).u8(s.focusCycles);
  return w.b;
}

function unpackSettingsAt(b) {
  var r = new Reader(b), utc = r.u32(), ver = r.u8(), s = {}, k;
  if (ver !== V1 && ver !== V2) throw new Error('bad version');
  for (k in SETTINGS_DEFAULT) s[k] = SETTINGS_DEFAULT[k];
  s.laterTask = r.u8(); s.laterReminder = r.u8(); s.headsupImportant = r.u8(); s.headsupCritical = r.u8();
  s.catchup = r.u8();
  if (ver === V2) {
    s.eventTime = r.u16(); s.laterEvent = r.u8(); s.focusWork = r.u8(); s.focusShort = r.u8();
    s.focusLong = r.u8(); s.focusCycles = r.u8();
  }
  return { settings: s, utc: utc };
}

function unpackLogs(b) {
  var r = new Reader(b), out = [];
  r.version(V1);
  for (var n = r.u8(), i = 0; i < n; i++) out.push({ t: r.i32(), item: r.u16(), step: r.u8(), v: r.i32() });
  return out;
}

function unpackHistory(b) {
  var r = new Reader(b), out = [];
  r.version(V1);
  var day = r.i32();
  for (var n = r.u8(), i = 0; i < n; i++) out.push({ id: r.u16(), status: r.u8(), count: r.u8(), missed: r.u8() });
  return { day: day, out: out };
}

function noteParts(textLen) {
  return textLen <= NOTE_HEAD_TEXT ? 1 : 1 + Math.ceil((textLen - NOTE_HEAD_TEXT) / NOTE_PART_TEXT);
}

function unpackNoteHead(b) {
  var r = new Reader(b);
  r.version(V1);
  var n = { id: r.u32(), len: r.u16(), link: r.u16(), status: r.u8(), reason: r.u8(), flags: r.u8(), textLen: r.u16() };
  n.name = r.str();
  n.bytes = r.bytes(Math.min(n.textLen, NOTE_HEAD_TEXT));
  return n;
}

function unpackNotePart(b) {
  var r = new Reader(b);
  r.version(V1);
  var id = r.u32(), part = r.u8();
  return { id: id, part: part, bytes: r.bytes(b.length - r.pos) };
}

// The whole note once every part is in (any order), else null. Characters may straddle records,
// so the text is decoded only after the bytes are joined.
function noteFinish(head, parts) {
  var bytes = head.bytes.slice(), want = noteParts(head.textLen);
  for (var p = 1; p < want; p++) {
    var found = null;
    for (var i = 0; i < parts.length; i++) if (parts[i].id === head.id && parts[i].part === p) found = parts[i];
    if (!found) return null;
    bytes = bytes.concat(found.bytes);
  }
  if (bytes.length !== head.textLen) return null;
  return { id: head.id, len: head.len, link: head.link, status: head.status, reason: head.reason, flags: head.flags,
    name: head.name, text: utf8Decode(bytes) };
}

function packTombs(ids) {
  var w = new Writer();
  w.u8(ids.length);
  for (var i = 0; i < ids.length; i++) w.u32(ids[i]);
  return w.b;
}

function unpackTombs(b) {
  var r = new Reader(b), out = [];
  for (var n = r.u8(), i = 0; i < n; i++) out.push(r.u32());
  return out;
}

function packRename(id, name) { return new Writer().u32(id).str(name, NAME_MAX).b; }
function packDelete(id) { return new Writer().u32(id).b; }

// Frames: [type u8][len u16][rec], back to back, at most DATA_MAX bytes per message.
function framesPut(list) {
  var msgs = [], cur = [];
  for (var i = 0; i < list.length; i++) {
    var f = list[i], len = f.rec.length;
    if (cur.length && cur.length + 3 + len > DATA_MAX) { msgs.push(cur); cur = []; }
    cur.push(f.type, len & 255, (len >>> 8) & 255);
    for (var j = 0; j < len; j++) cur.push(f.rec[j]);
  }
  if (cur.length) msgs.push(cur);
  return msgs;
}

function framesRead(b) {
  var out = [], pos = 0;
  while (pos < b.length) {
    if (pos + 3 > b.length) throw new Error('torn frame');
    var len = b[pos + 1] | (b[pos + 2] << 8);
    if (pos + 3 + len > b.length) throw new Error('short frame');
    out.push({ type: b[pos], rec: b.slice(pos + 3, pos + 3 + len) });
    pos += 3 + len;
  }
  return out;
}

module.exports = {
  T: T, DATA_MAX: DATA_MAX, NAME_MAX: NAME_MAX, UNIT_MAX: UNIT_MAX, SETTINGS_DEFAULT: SETTINGS_DEFAULT,
  utf8Cut: utf8Cut, utf8Bytes: utf8Bytes, utf8Encode: utf8Encode, utf8Decode: utf8Decode,
  packItem: packItem, unpackItem: unpackItem, packStepAt: packStepAt, unpackStepAt: unpackStepAt,
  packSettingsAt: packSettingsAt, unpackSettingsAt: unpackSettingsAt, unpackLogs: unpackLogs,
  unpackHistory: unpackHistory, noteParts: noteParts, unpackNoteHead: unpackNoteHead, unpackNotePart: unpackNotePart,
  noteFinish: noteFinish, packTombs: packTombs, unpackTombs: unpackTombs, packRename: packRename,
  packDelete: packDelete, framesPut: framesPut, framesRead: framesRead
};
