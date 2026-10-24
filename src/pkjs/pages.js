// What PebbleKit JS opens: the planner, or one part of an export, each as a data: URL. Phone apps
// cap URL length (Chromium at 2 MB; an Android Intent carries about 1 MB, two bytes a character),
// so every page stays under URL_MAX characters however much the phone has kept.
var codec = require('./codec.js');

var PREFIX = 'data:text/html;charset=utf-8,';
var URL_MAX = 400000;
var MIN_BUDGET = 16 * 1024;
var TITLES = { notes: 'Voice notes', numbers: 'Numbers (CSV)', days: 'Days (CSV)' };

function dataScript(data) {
  return 'window.COACH=' + JSON.stringify(data).replace(/</g, '\\u003c') + ';';
}

function url(page, data) {
  var script = dataScript(data);
  return PREFIX + encodeURIComponent(page.replace('/*DATA*/', function () { return script; }));
}

// Percent-encoding works character by character, so a page's URL length is the page's plus the data's.
function urlLength(pageLen, data) { return PREFIX.length + pageLen + encodeURIComponent(dataScript(data)).length; }
function pageLength(page) { return encodeURIComponent(page.replace('/*DATA*/', '')).length; }

// Shrinks a byte budget until every page made with it fits (non-ASCII text triples when encoded).
function fit(budget, fits) {
  while (budget > MIN_BUDGET && !fits(budget)) budget = Math.floor(budget * 0.7);
  return budget;
}

function plannerUrl(page, store, now) {
  var len = pageLength(page);
  var b = fit(200 * 1024, function (b) { return urlLength(len, store.plannerData(now, b)) <= URL_MAX; });
  return url(page, store.plannerData(now, b));
}

// One export as text parts of at most maxBytes, cut between notes or between CSV rows; every CSV
// part starts with the header.
function exportParts(M, store, kind, maxBytes) {
  var header = '', joiner = '\n', recs;
  if (kind === 'notes') {
    joiner = '\n\n';
    recs = store.notes.map(function (n) { return M.textNotes([n]); });
  } else {
    var csv = kind === 'numbers' ? M.csvNumbers : M.csvOutcomes, list = kind === 'numbers' ? store.logs : store.hist;
    header = csv([]);
    recs = list.map(function (e) { return csv([e]).slice(header.length + 1); }).filter(function (r) { return r; });
  }
  var base = header ? codec.utf8Bytes(header) + 1 : 0, parts = [], cur = [], size = base;
  recs.forEach(function (r) {
    var b = codec.utf8Bytes(r) + joiner.length;
    if (cur.length && size + b > maxBytes) { parts.push(cur); cur = []; size = base; }
    cur.push(r);
    size += b;
  });
  if (cur.length || !parts.length) parts.push(cur);
  return parts.map(function (c) {
    var body = c.join(joiner);
    return header ? (body ? header + '\n' + body : header) : body;
  });
}

function exportData(kind, parts, part) {
  var k = Math.max(1, Math.min(part || 1, parts.length));
  return { v: 1, mode: 'export', kind: kind, title: TITLES[kind] || kind, part: k, parts: parts.length, text: parts[k - 1] };
}

// One budget for the whole export, so part N is the same text whichever part was asked for first.
function exportUrl(page, M, store, kind, part) {
  var len = pageLength(page);
  var b = fit(150 * 1024, function (b) {
    var parts = exportParts(M, store, kind, b);
    for (var i = 1; i <= parts.length; i++) if (urlLength(len, exportData(kind, parts, i)) > URL_MAX) return false;
    return true;
  });
  return url(page, exportData(kind, exportParts(M, store, kind, b), part));
}

module.exports = { PREFIX: PREFIX, URL_MAX: URL_MAX, plannerUrl: plannerUrl, exportParts: exportParts, exportUrl: exportUrl };
