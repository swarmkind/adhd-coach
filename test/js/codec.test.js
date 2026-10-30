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
  it.steps = V.steps.filter(function (s) { return s.item === it.id; }).map(function (s) { return s.json; });
  assert.equal(toHex(codec.packItem(it)), V.items[0].hex);
});

test('a long name is cut at 31 bytes like the watch', function () {
  var it = JSON.parse(JSON.stringify(V.items[2].json));
  it.name = 'Café with Zoë 🎂 and the long name';
  assert.equal(toHex(codec.packItem(it)), V.items[2].hex);
});

test('steps carry their item and index', function () {
  V.steps.forEach(function (v) {
    assert.equal(toHex(codec.packStepAt(v.item, v.index, v.json, v.alt)), v.hex);
    assert.deepEqual(codec.unpackStepAt(fromHex(v.hex)), { item: v.item, index: v.index, alt: v.alt, step: v.json });
  });
});

test('1.4.1 records read with no step-ups, alternatives or rhythm', function () {
  // item 1, index 0, V1 timed step: 90 s, no number, "Plank"
  var b = [1, 0, 0, 1, 1, 90, 0].concat([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], [5], codec.utf8Encode('Plank'));
  var u = codec.unpackStepAt(b);
  assert.equal(u.alt, null);
  assert.deepEqual(u.step.amount, { start: 90, cap: 0, since: 0, inc: 0, every: 0 });
  assert.deepEqual(u.step.show, { every: 0, since: 0 });
  var it = codec.unpackItem(fromHex(V.items[3].hex).map(function (x, i) { return i === 0 ? 1 : x; }).slice(0, -6));
  assert.equal(it.name, 'Tidy desk');
  assert.equal(it.altCount, 0);
  assert.deepEqual(it.repeat, { every: 0, since: 0 });
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

// The Workout vectors: Curls (alternative Shoulder press at record 2) and Plank.
function workout() {
  var v = V.steps.filter(function (s) { return s.item === 2; });
  var it = JSON.parse(JSON.stringify(V.items[5].json));
  delete it.stepCount; delete it.altCount;
  var press = JSON.parse(JSON.stringify(v[2].json));
  delete press.show;
  it.steps = [JSON.parse(JSON.stringify(v[0].json)), JSON.parse(JSON.stringify(v[1].json))];
  it.steps[0].alt = press;
  it.steps[1].alt = null;
  return { item: it, hex: v.map(function (s) { return s.hex; }) };
}

test('alternatives go out after the main steps, as the C records lay them out', function () {
  var w = workout();
  assert.equal(w.item.name, 'Workout');
  assert.equal(toHex(codec.packItem(w.item)), V.items[5].hex);
  assert.deepEqual(codec.stepRecords(w.item).map(toHex), w.hex);
});

test('withSteps nests each alternative back into its main step', function () {
  var w = workout();
  var flat = w.hex.map(function (h) { return codec.unpackStepAt(fromHex(h)); });
  var items = codec.withSteps([codec.unpackItem(fromHex(V.items[5].hex))], flat);
  assert.equal(items[0].steps.length, 2);
  assert.equal(items[0].steps[0].alt.name, 'Shoulder press');
  assert.ok(!('show' in items[0].steps[0].alt) && !('alt' in items[0].steps[0].alt));
  assert.equal(items[0].steps[1].alt, null);
  assert.ok(!('stepCount' in items[0]) && !('altCount' in items[0]));
  assert.deepEqual(items[0].steps, w.item.steps);
});
