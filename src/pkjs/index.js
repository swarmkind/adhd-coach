// Coach's phone side: keeps the master copy of the schedule, logs and notes in localStorage,
// syncs with the watch whenever Coach opens, and opens the bundled planner.
var Store = require('./store.js');
var Session = require('./sync.js');
var pages = require('./pages.js');
var plannerPage = require('./planner_page.js');
var model = require('./planner_model.js');

var store = new Store(localStorage);
var timers = {
  set: function (fn, ms) { return setTimeout(fn, ms); },
  clear: function (id) { clearTimeout(id); },
  now: function () { return Math.floor(Date.now() / 1000); }
};
function log(s) { console.log(s); }
function send(msg, ok, fail) {
  Pebble.sendAppMessage(msg, ok, function (e) { log('sync: nack ' + JSON.stringify(e && e.error)); fail(); });
}
var session = new Session(store, send, timers, log);

Pebble.addEventListener('ready', function () {
  log('coach js ready: rev ' + store.sched.rev + ', ' + store.notes.length + ' notes, ' + store.logs.length + ' logs');
  session.hello();
});

Pebble.addEventListener('appmessage', function (e) { session.onMessage(e.payload); });

// The planner is bundled, so it opens with the phone app's network permission off. An export too big
// for the planner opens as its own page; `pending` keeps it for the next Settings tap in case the phone
// app cannot open a page straight after another one closes.
var pending = null;

function openExport(kind, part) {
  pending = { kind: kind, part: part };
  var url = pages.exportUrl(plannerPage, model, store, kind, part);
  log('export: ' + kind + ' part ' + part + ', ' + url.length + ' characters');
  Pebble.openURL(url);
}

Pebble.addEventListener('showConfiguration', function () {
  if (pending) { openExport(pending.kind, pending.part); return; }
  var url = pages.plannerUrl(plannerPage, store, Date.now());
  log('planner: opening, ' + url.length + ' characters');
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function (e) {
  var wasExport = !!pending;
  pending = null;
  if (!e || !e.response) { log(wasExport ? 'export: closed' : 'planner: closed without saving'); return; }
  var res;
  try { res = JSON.parse(decodeURIComponent(e.response)); } catch (err) {
    try { res = JSON.parse(e.response); } catch (err2) { log('planner: unreadable response'); return; }
  }
  if (res.items || res.settings || (res.ops && res.ops.length)) {
    var r = store.applyPlanner(res, timers.now());
    log('planner: saved, ' + (r.changed ? 'schedule rev ' + store.sched.rev : 'schedule unchanged') + ', ' + r.ops + ' note edits');
    session.hello();
  }
  if (res.export) openExport(res.export, res.part || 1);
});
