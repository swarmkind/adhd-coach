var test = require('node:test');
var assert = require('node:assert/strict');
var Store = require('../../src/pkjs/store.js');
var pages = require('../../src/pkjs/pages.js');
var M = require('../../planner/model.js');
var codec = require('../../src/pkjs/codec.js');
var H = require('./helpers.js');

// The real page is about 54 KB of HTML around one /*DATA*/ slot.
var PAGE = '<!doctype html><script>/*DATA*/</script>' + new Array(54001).join('x');
var NOTE = 'Remember to call the garage about the brakes and ask whether they can fit it in on Thursday. ' +
  'The noise is getting worse on the hill, and the front left wheel squeals when it is wet, so mention that too.';

function heavy(years, text) {
  var st = new Store(new H.MemStorage());
  st.adopt(H.sampleItems(), H.clone(codec.SETTINGS_DEFAULT), 0);
  H.yearsOfUse(st, years, text || NOTE);
  return st;
}

function coach(url) {
  assert.equal(url.indexOf(pages.PREFIX), 0);
  var html = decodeURIComponent(url.slice(pages.PREFIX.length));
  var at = html.indexOf('window.COACH=') + 'window.COACH='.length;
  return JSON.parse(html.slice(at, html.indexOf(';</script>')));
}

test('the planner URL stays under the limit after three years of heavy use', function () {
  var st = heavy(3);
  var url = pages.plannerUrl(PAGE, st, 1234);
  assert.ok(url.length <= pages.URL_MAX, 'URL is ' + url.length);
  var d = coach(url);
  assert.equal(d.items.length, 3);
  assert.equal(d.now, 1234);
  assert.ok(d.more.notes > 0 && d.more.logs > 0 && d.more.days > 0);
});

test('text that percent-encodes badly still fits: the budget shrinks until the URL does', function () {
  var st = heavy(1, new Array(120).join('été \u{1F331} '));
  var url = pages.plannerUrl(PAGE, st, 1);
  assert.ok(url.length <= pages.URL_MAX, 'URL is ' + url.length);
  assert.ok(coach(url).notes.length > 0);
});

test('a small phone gets the whole planner in one page with nothing left out', function () {
  var st = heavy(0.05);
  var d = coach(pages.plannerUrl(PAGE, st, 1));
  assert.deepEqual(d.more, { notes: 0, logs: 0, days: 0 });
});

test('exported notes come in parts that cover every note once, in order', function () {
  var st = heavy(1);
  var parts = pages.exportParts(M, st, 'notes', 40 * 1024);
  assert.ok(parts.length > 1);
  parts.forEach(function (p) { assert.ok(codec.utf8Bytes(p) <= 40 * 1024); });
  assert.equal(parts.join('\n\n'), M.textNotes(st.notes));
});

test('exported numbers and days repeat the CSV header in every part and lose no row', function () {
  var st = heavy(1);
  ['numbers', 'days'].forEach(function (kind) {
    var whole = kind === 'numbers' ? M.csvNumbers(st.logs) : M.csvOutcomes(st.hist);
    var header = whole.slice(0, whole.indexOf('\n'));
    var parts = pages.exportParts(M, st, kind, 30 * 1024);
    assert.ok(parts.length > 1, kind);
    var rows = [];
    parts.forEach(function (p) {
      assert.ok(codec.utf8Bytes(p) <= 30 * 1024);
      assert.equal(p.slice(0, header.length + 1), header + '\n');
      rows.push(p.slice(header.length + 1));
    });
    assert.equal(header + '\n' + rows.join('\n'), whole);
  });
});

test('an export page carries its part, the number of parts and the text, under the URL limit', function () {
  var st = heavy(3);
  var url = pages.exportUrl(PAGE, M, st, 'notes', 2);
  assert.ok(url.length <= pages.URL_MAX, 'URL is ' + url.length);
  var d = coach(url);
  assert.equal(d.mode, 'export');
  assert.equal(d.kind, 'notes');
  assert.equal(d.part, 2);
  assert.ok(d.parts > 2);
  assert.equal(d.title, 'Voice notes');
  assert.ok(d.text.length > 1000);
  // A part past the end falls back to the last one.
  assert.equal(coach(pages.exportUrl(PAGE, M, st, 'notes', 999)).part, d.parts);
});
