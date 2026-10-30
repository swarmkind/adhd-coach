#!/usr/bin/env node
// Inlines planner/ into one HTML string for PebbleKit JS (src/pkjs/planner_page.js, and the model as
// src/pkjs/planner_model.js), which the wscript runs before every build. `--preview out.html` writes a
// standalone page with sample data.
var fs = require('fs');
var path = require('path');

var root = path.join(__dirname, '..');
function read(f) { return fs.readFileSync(path.join(root, 'planner', f), 'utf8'); }

function page() {
  var js = read('model.js') + '\n' + read('app.js');
  if (/<\/script/i.test(js)) throw new Error('planner script contains </script');
  return read('index.html')
    .replace('/*CSS*/', function () { return read('planner.css'); })
    .replace('/*JS*/', function () { return js; });
}

// What PebbleKit JS does when it opens the page.
function inject(html, data) {
  var json = JSON.stringify(data).replace(/</g, '\\u003c');
  return html.replace('/*DATA*/', function () { return 'window.COACH=' + json + ';'; });
}

function sample() {
  var NONE = { step: 0, min: 0, max: 0, decimals: 0, unit: '' };
  var now = Math.floor(Date.now() / 1000), day = Math.floor((now - new Date().getTimezoneOffset() * 60) / 86400);
  var items = [
    { id: 1, kind: 0, priority: 0, flags: 0, days: 62, at: 420, until: 0, every: 0, goal: 0, grace: 15, order: 0, eventDay: 0,
      ladder: [], num: NONE, name: 'Morning routine', steps: [
        { type: 0, dur: 0, num: NONE, name: 'Drink a glass of water' },
        { type: 1, dur: 60, num: NONE, name: 'Plank' },
        { type: 2, dur: 0, num: { step: 1, min: 300, max: 2500, decimals: 1, unit: 'kg' }, name: 'Weight' },
        { type: 1, dur: 120, num: NONE, name: 'Stretch hamstrings and hips' }] },
    { id: 2, kind: 1, priority: 0, flags: 0, days: 127, at: 540, until: 1260, every: 60, goal: 8, grace: 0, order: 1,
      eventDay: 0, ladder: [], num: NONE, name: 'Drink water', steps: [] },
    { id: 3, kind: 1, priority: 0, flags: 4, days: 127, at: 600, until: 1200, every: 120, goal: 0, grace: 0, order: 2,
      eventDay: 0, ladder: [], num: { step: 1, min: 0, max: 500, decimals: 0, unit: 'reps' }, name: '10 pushups', steps: [] },
    { id: 4, kind: 2, priority: 1, flags: 0, days: 4, at: 840, until: 0, every: 0, goal: 0, grace: 0, order: 3, eventDay: 0,
      ladder: [], num: NONE, name: 'Call the dentist', steps: [] },
    { id: 5, kind: 2, priority: 0, flags: 2, days: 127, at: 0, until: 0, every: 0, goal: 0, grace: 0, order: 4, eventDay: 0,
      ladder: [], num: NONE, name: 'Tidy desk', steps: [] },
    { id: 7, kind: 2, priority: 0, flags: 2, days: 127, at: 0, until: 0, every: 0, goal: 0, grace: 0, order: 5, eventDay: 0,
      ladder: [], num: NONE, name: 'Fix the bike light', steps: [] },
    { id: 6, kind: 3, priority: 1, flags: 0, days: 0, at: 630, until: 0, every: 0, goal: 0, grace: 0, order: 6,
      eventDay: day + 8, ladder: [7, 1, 0], num: NONE, name: 'Dentist check-up', steps: [] }
  ];
  var logs = [];
  for (var i = 9; i >= 0; i--) {
    logs.push({ t: now - i * 86400 - 3600, item: 1, step: 2, v: 824 - i * 2 + (i % 3), label: 'Morning routine · Weight', unit: 'kg', dec: 1 });
    if (i % 2) logs.push({ t: now - i * 86400, item: 3, step: 255, v: 10 + i, label: '10 pushups', unit: 'reps', dec: 0 });
    if (i % 3 === 0) {  // curls: a quantity and its weight, logged together
      logs.push({ t: now - i * 86400 - 3000, item: 1, step: 4, v: 18 - i, label: 'Morning routine · Curls', unit: '', dec: 0 });
      logs.push({ t: now - i * 86400 - 2999, item: 1, step: 0x44, v: 125, label: 'Morning routine · Curls', unit: 'kg', dec: 1 });
    }
  }
  var hist = [];
  for (var d = 7; d >= 1; d--) {
    hist.push({ day: day - d, out: [{ id: 1, status: d % 3 ? 2 : 3, count: 0, missed: 0, name: 'Morning routine', kind: 0 },
      { id: 2, status: 0, count: 8 - (d % 4), missed: d % 4, name: 'Drink water', kind: 1 },
      { id: 4, status: d === 2 ? 4 : 2, count: 0, missed: 0, name: 'Call the dentist', kind: 2 }] });
  }
  var notes = [
    { id: now - 3 * 86400, len: 41, link: 0, linkName: '', status: 0, reason: 0, name: 'Garden ideas',
      text: 'Move the beans to the sunny side, and try the tomatoes in pots this year so they can come in if it gets cold.' },
    { id: now - 86400 - 5000, len: 4, link: 0, linkName: '', status: 1, reason: 5, name: '', text: '' },
    { id: now - 7200, len: 12, link: 1, linkName: 'Morning routine', status: 0, reason: 0, name: '',
      text: 'Put the weigh-in before the stretches, because after the stretches I always forget it.' }
  ];
  var settings = { laterTask: 30, laterReminder: 10, headsupImportant: 10, headsupCritical: 15, catchup: 10,
    eventTime: 540, laterEvent: 120, focusWork: 25, focusShort: 5, focusLong: 15, focusCycles: 4 };
  return { v: 1, nextId: 8, items: items, settings: settings, notes: notes, logs: logs, hist: hist, now: now * 1000,
    lastSync: now - 600 };
}

var html = page();
var at = process.argv.indexOf('--preview');
if (at >= 0) {
  fs.writeFileSync(process.argv[at + 1], inject(html, sample()));
} else {
  fs.writeFileSync(path.join(root, 'src', 'pkjs', 'planner_page.js'),
    '// Generated by tools/build-planner.js from planner/. Do not edit.\nmodule.exports = ' + JSON.stringify(html) + ';\n');
  // PebbleKit JS builds the big exports with the page's own text and CSV code.
  fs.writeFileSync(path.join(root, 'src', 'pkjs', 'planner_model.js'),
    '// Generated by tools/build-planner.js from planner/model.js. Do not edit.\n' + read('model.js'));
}
module.exports = { page: page, inject: inject, sample: sample };
