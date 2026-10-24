// codec.js against the bytes the watch's C records produce (test/build/vectors.json).
var test = require('node:test');
var assert = require('node:assert/strict');
var codec = require('../../src/pkjs/codec.js');
var V = require('../build/vectors.json');

function fromHex(h) {
  var out = [];
  for (var i = 0; i < h.length; i += 2) out.push(parseInt(h.substr(i, 2), 16));
  return out;
}
function toHex(b) {
  return b.map(function (x) { return (x < 16 ? '0' : '') + x.toString(16); }).join('');
}

test('items pack and unpack like the C records', function () {
  V.items.forEach(function (v) {
    assert.equal(toHex(codec.packItem(v.json)), v.hex, v.json.name);
    assert.deepEqual(codec.unpackItem(fromHex(v.hex)), v.json);
  });
});

test('an item packed from steps writes their count', function () {
  var it = JSON.parse(JSON.stringify(V.items[0].json));
  delete it.stepCount;
  it.steps = V.steps.map(function (s) { return s.json; });
  assert.equal(toHex(codec.packItem(it)), V.items[0].hex);
});

test('a long name is cut at 31 bytes like the watch', function () {
  var it = JSON.parse(JSON.stringify(V.items[2].json));
  it.name = 'Café with Zoë 🎂 and the long name';
  assert.equal(toHex(codec.packItem(it)), V.items[2].hex);
});

test('steps carry their item and index', function () {
  V.steps.forEach(function (v) {
    assert.equal(toHex(codec.packStepAt(v.item, v.index, v.json)), v.hex);
    assert.deepEqual(codec.unpackStepAt(fromHex(v.hex)), { item: v.item, index: v.index, step: v.json });
  });
});

test('settings carry their edit time', function () {
  var v = V.settings;
  assert.equal(toHex(codec.packSettingsAt(v.json, v.utc)), v.hex);
  assert.deepEqual(codec.unpackSettingsAt(fromHex(v.hex)), { settings: v.json, utc: v.utc });
});

test('log chunks and history days unpack', function () {
  assert.deepEqual(codec.unpackLogs(fromHex(V.logs.hex)), V.logs.json);
  assert.deepEqual(codec.unpackHistory(fromHex(V.history.hex)), V.history.json);
});

test('a note reassembles from its head and parts, characters split across records', function () {
  var recs = V.note.records.map(fromHex);
  var head = codec.unpackNoteHead(recs[0]);
  var parts = recs.slice(1).map(codec.unpackNotePart);
  parts.forEach(function (p, i) { assert.equal(p.id, V.note.json.id); assert.equal(p.part, i + 1); });
  assert.equal(codec.noteParts(head.textLen), recs.length);
  assert.deepEqual(codec.noteFinish(head, parts), V.note.json);
  assert.deepEqual(codec.noteFinish(head, parts.slice().reverse()), V.note.json);
  assert.equal(codec.noteFinish(head, parts.slice(1)), null);  // a part is missing
  var missed = codec.unpackNoteHead(fromHex(V.missed.hex));
  assert.deepEqual(codec.noteFinish(missed, []), V.missed.json);
});

test('tombs, renames and deletes', function () {
  assert.equal(toHex(codec.packTombs(V.tombs.json)), V.tombs.hex);
  assert.deepEqual(codec.unpackTombs(fromHex(V.tombs.hex)), V.tombs.json);
  assert.equal(toHex(codec.packRename(V.rename.id, V.rename.name)), V.rename.hex);
  assert.equal(toHex(codec.packDelete(1792800000)), '00f5db6a');
});

test('frames read the C layout and split messages at 900 bytes', function () {
  var read = codec.framesRead(fromHex(V.frames.hex));
  assert.deepEqual(read.map(function (f) { return { type: f.type, rec: toHex(f.rec) }; }), V.frames.json);
  var item = fromHex(V.items[0].hex);
  var list = [];
  for (var i = 0; i < 30; i++) list.push({ type: codec.T.ITEM, rec: item });
  var msgs = codec.framesPut(list);
  assert.ok(msgs.length > 1);
  var back = [];
  msgs.forEach(function (m) {
    assert.ok(m.length <= codec.DATA_MAX, 'message of ' + m.length);
    back = back.concat(codec.framesRead(m));
  });
  assert.equal(back.length, 30);
  back.forEach(function (f) { assert.equal(toHex(f.rec), V.items[0].hex); });
  assert.throws(function () { codec.framesRead([1, 5, 0, 1]); });
});

test('utf8Cut never splits a character', function () {
  var s = 'abcé🌱';  // 3 + 2 + 4 bytes
  assert.equal(codec.utf8Cut(s, 4), 'abc');
  assert.equal(codec.utf8Cut(s, 5), 'abcé');
  assert.equal(codec.utf8Cut(s, 8), 'abcé');
  assert.equal(codec.utf8Cut(s, 9), s);
  assert.equal(codec.utf8Bytes(s), 9);
});
