var test = require('node:test');
var assert = require('node:assert/strict');
var H = require('./helpers.js');
var pages = require('../../src/pkjs/pages.js');

// index.js against a fake Pebble: which page each Settings tap and each close opens.
// Its sync timers are unref'd so they don't hold the test process open.
var realSetTimeout = setTimeout;
global.setTimeout = function (fn, ms) { var t = realSetTimeout(fn, ms); if (t.unref) t.unref(); return t; };

function boot(fill) {
  var handlers = {}, opened = [];
  global.localStorage = new H.MemStorage();
  if (fill) fill(global.localStorage);
  global.Pebble = {
    addEventListener: function (name, fn) { handlers[name] = fn; },
    openURL: function (u) { opened.push(u); },
    sendAppMessage: function (msg, ok) { ok(); }
  };
  var path = require.resolve('../../src/pkjs/index.js');
  delete require.cache[path];
  var origLog = console.log;
  console.log = function () {};
  try { require(path); } finally { console.log = origLog; }
  function fire(name, e) {
    var origLog2 = console.log;
    console.log = function () {};
    try { handlers[name](e); } finally { console.log = origLog2; }
  }
  return { fire: fire, opened: opened };
}

function coach(url) {
  var html = decodeURIComponent(url.slice(pages.PREFIX.length));
  var at = html.indexOf('window.COACH=') + 'window.COACH='.length;
  return JSON.parse(html.slice(at, html.indexOf(';</script>')));
}

function closeWith(obj) { return { response: encodeURIComponent(JSON.stringify(obj)) }; }

test('Settings opens the planner; an export request opens that export, and Next part the next one', function () {
  var w = boot(function (ls) {
    var notes = [];
    for (var i = 0; i < 3000; i++) notes.push({ id: 1761200000 + i * 3600, len: 9, link: 0, linkName: '', status: 0, reason: 0, name: '',
      text: new Array(30).join('a longer voice note ') });
    ls.setItem('notes', JSON.stringify(notes));
  });
  w.fire('showConfiguration');
  var p = coach(w.opened[0]);
  assert.equal(p.mode, undefined);
  assert.ok(p.more.notes > 0);
  w.fire('webviewclosed', closeWith({ v: 1, export: 'notes' }));
  var e1 = coach(w.opened[1]);
  assert.equal(e1.mode, 'export');
  assert.equal(e1.part, 1);
  assert.ok(e1.parts > 1);
  w.fire('webviewclosed', closeWith({ v: 1, export: 'notes', part: 2 }));
  assert.equal(coach(w.opened[2]).part, 2);
  w.fire('webviewclosed', { response: '' });  // Done
  w.fire('showConfiguration');
  assert.equal(coach(w.opened[3]).mode, undefined, 'after Done, Settings is the planner again');
});

test('if the phone app could not show the export straight away, the next Settings tap shows it', function () {
  var w = boot();
  w.fire('webviewclosed', closeWith({ v: 1, export: 'numbers' }));
  w.fire('showConfiguration');
  assert.equal(coach(w.opened[1]).mode, 'export');
  assert.equal(coach(w.opened[1]).kind, 'numbers');
});

test('an export request carrying planner edits saves them first', function () {
  var w = boot();
  w.fire('webviewclosed', closeWith({ v: 1, nextId: 2, items: [], settings: { focusWork: 30 }, ops: [], export: 'days' }));
  var sched = JSON.parse(global.localStorage.getItem('sched'));
  assert.equal(sched.settings.focusWork, 30);
  assert.equal(sched.rev, 1);
  assert.equal(coach(w.opened[0]).kind, 'days');
});
