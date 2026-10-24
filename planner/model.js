// The planner's pure model: item defaults, validation against what the watch accepts,
// unit conversions, and the export formats. Runs in the page and in node tests.
(function (root) {
  var KIND = { ROUTINE: 0, REMINDER: 1, TASK: 2, EVENT: 3 };
  var FLAG = { SILENT: 1, ANYTIME: 2, ASK: 4 };
  var STEP = { MANUAL: 0, TIMED: 1, NUMBER: 2 };
  var LADDERS = { gentle: [1, 0], normal: [7, 1, 0], persistent: [21, 7, 3, 1, 0, -1] };
  var MAX_ITEMS = 40, MAX_STEPS = 12, MAX_RUNGS = 6, NAME_BYTES = 31, UNIT_BYTES = 5;
  var STATUS = ['not done', 'started', 'done', 'skipped', 'missed', 'paused'];
  var WDAY = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  var MONTH = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

  function utf8(s) { return new TextEncoder().encode(s || ''); }
  function utf8Bytes(s) { return utf8(s).length; }
  function utf8Cut(s, max) {
    var b = utf8(s);
    if (b.length <= max) return s || '';
    var cut = max;
    while (cut > 0 && (b[cut] & 0xC0) === 0x80) cut--;
    return new TextDecoder().decode(b.slice(0, cut));
  }

  function num(step, min, max, decimals, unit) { return { step: step, min: min, max: max, decimals: decimals, unit: unit }; }
  var NO_NUM = num(0, 0, 0, 0, '');

  function newStep(type) {
    return { type: type, dur: type === STEP.TIMED ? 60 : 0, num: type === STEP.NUMBER ? num(1, 0, 1000, 0, '') : NO_NUM,
      name: '' };
  }

  function newItem(kind, id, today) {
    var it = { id: id, kind: kind, priority: 0, flags: 0, days: 0x7F, at: 540, until: 0, every: 0, goal: 0, grace: 0,
      order: 0, eventDay: 0, ladder: [], num: NO_NUM, name: '', steps: [] };
    if (kind === KIND.ROUTINE) { it.days = 0x3E; it.at = 420; }
    if (kind === KIND.REMINDER) { it.until = 1260; it.every = 60; it.num = num(1, 0, 500, 0, ''); }
    if (kind === KIND.TASK) it.flags = FLAG.ANYTIME;
    if (kind === KIND.EVENT) { it.days = 0; it.at = 600; it.eventDay = today + 7; it.ladder = LADDERS.normal.slice(); }
    return it;
  }

  function numErrors(n, what) {
    var e = [];
    if (!(n.step > 0)) e.push(what + 'the step must be above zero.');
    if (n.min > n.max) e.push(what + 'the lowest value is above the highest.');
    return e;
  }

  // Messages for anything the watch would refuse or that could never fire. Empty = fine.
  function validate(it) {
    var e = [];
    if (!(it.name || '').trim()) e.push('Give it a name.');
    var timed = it.kind === KIND.ROUTINE || it.kind === KIND.REMINDER || (it.kind === KIND.TASK && !(it.flags & FLAG.ANYTIME));
    if (timed && !(it.days & 0x7F)) e.push('Pick at least one day.');
    if (it.kind === KIND.REMINDER) {
      if (it.until < it.at) e.push('The window ends before it starts.');
      if (!(it.every > 0)) e.push('Repeat at least every minute.');
      if (it.flags & FLAG.ASK) e = e.concat(numErrors(it.num, 'The number: '));
    }
    if (it.kind === KIND.EVENT) {
      if (!(it.eventDay > 0)) e.push('Pick a date.');
      if (!it.ladder.length) e.push('Pick at least one reminder.');
      if (it.ladder.length > MAX_RUNGS) e.push('At most ' + MAX_RUNGS + ' reminders.');
    }
    var steps = it.steps || [];
    if (steps.length > MAX_STEPS) e.push('At most ' + MAX_STEPS + ' steps.');
    for (var i = 0; i < steps.length; i++) {
      var s = steps[i], n = 'Step ' + (i + 1);
      if (!(s.name || '').trim()) e.push(n + ' needs a name.');
      if (s.type === STEP.TIMED && !(s.dur > 0)) e.push(n + ' needs a length.');
      if (s.type === STEP.NUMBER) e = e.concat(numErrors(s.num, n + ': '));
    }
    return e;
  }

  function cleanNum(n) {
    return num(n.step | 0, n.min | 0, n.max | 0, Math.max(0, Math.min(4, n.decimals | 0)), utf8Cut(n.unit, UNIT_BYTES));
  }

  // What the watch stores: names cut on a character boundary, numbers in range.
  function clean(it) {
    var out = JSON.parse(JSON.stringify(it));
    out.name = utf8Cut((out.name || '').trim(), NAME_BYTES);
    out.num = cleanNum(out.num || NO_NUM);
    out.steps = (out.steps || []).map(function (s) {
      return { type: s.type, dur: Math.max(0, Math.min(65535, s.dur | 0)), num: cleanNum(s.num || NO_NUM),
        name: utf8Cut((s.name || '').trim(), NAME_BYTES) };
    });
    return out;
  }

  function ladderName(l) {
    for (var k in LADDERS) if (LADDERS[k].join() === (l || []).join()) return k;
    return 'custom';
  }

  function rungText(r) {
    if (r === 0) return 'the morning of';
    if (r === -1) return 'an hour before';
    if (r === 7) return 'a week before';
    if (r === 1) return 'the day before';
    return r + ' days before';
  }

  function pad(n) { return (n < 10 ? '0' : '') + n; }
  function minToHHMM(m) { return pad(Math.floor(m / 60)) + ':' + pad(m % 60); }
  function hhmmToMin(s) {
    var m = /^(\d{1,2}):(\d{2})$/.exec(s || '');
    if (!m || +m[1] > 23 || +m[2] > 59) return null;
    return +m[1] * 60 + +m[2];
  }

  // Days since 1970-01-01 in local calendar terms, as the watch's cc_day_of counts them.
  function isoToDay(iso) {
    var m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(iso || '');
    return m ? Math.round(Date.UTC(+m[1], +m[2] - 1, +m[3]) / 86400000) : null;
  }
  function dayToISO(day) {
    var d = new Date(day * 86400000);
    return d.getUTCFullYear() + '-' + pad(d.getUTCMonth() + 1) + '-' + pad(d.getUTCDate());
  }
  function dayText(day) {  // "Fri 14 Nov"
    var d = new Date(day * 86400000);
    return WDAY[d.getUTCDay()] + ' ' + d.getUTCDate() + ' ' + MONTH[d.getUTCMonth()];
  }

  // A UTC second as local fields. tzMin = minutes east of UTC; left out, the phone's offset at that
  // moment (so a February note keeps its winter time when read in July).
  function local(t, tzMin) {
    if (tzMin == null) tzMin = -new Date(t * 1000).getTimezoneOffset();
    var d = new Date((t + tzMin * 60) * 1000);
    return { day: Math.floor((t + tzMin * 60) / 86400), min: d.getUTCHours() * 60 + d.getUTCMinutes(), y: d.getUTCFullYear(),
      wday: WDAY[d.getUTCDay()], date: d.getUTCDate(), month: MONTH[d.getUTCMonth()] };
  }

  function fromScaled(v, dec) {
    if (!dec) return String(v);
    var neg = v < 0, a = Math.abs(v), p = Math.pow(10, dec);
    var s = Math.floor(a / p) + '.' + String(a % p + p).slice(1);
    return (neg ? '-' : '') + s;
  }
  function toScaled(s, dec) {
    var m = /^\s*(-?)(\d*)(?:\.(\d*))?\s*$/.exec(s || '');
    if (!m || (!m[2] && !m[3])) return null;
    var frac = ((m[3] || '') + '0000').slice(0, dec);
    var v = parseInt((m[2] || '0') + frac, 10);
    return m[1] ? -v : v;
  }

  function durText(sec) { return Math.floor(sec / 60) + ':' + pad(sec % 60); }

  var DAYS_SHORT = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  function daysText(bits) {
    bits &= 0x7F;
    if (bits === 0x7F) return 'every day';
    if (bits === 0x3E) return 'Mon to Fri';
    if (bits === 0x41) return 'weekends';
    var out = [];
    for (var i = 1; i <= 7; i++) if (bits & (1 << (i % 7))) out.push(DAYS_SHORT[i % 7]);  // Monday first
    return out.join(' ') || 'no days';
  }

  function summary(it) {
    var n = (it.steps || []).length;
    if (it.kind === KIND.ROUTINE) return daysText(it.days) + ' · ' + minToHHMM(it.at) + ' · ' + n + (n === 1 ? ' step' : ' steps');
    if (it.kind === KIND.REMINDER) {
      return 'every ' + it.every + ' min · ' + minToHHMM(it.at) + ' to ' + minToHHMM(it.until) +
        (it.goal ? ' · goal ' + it.goal : '');
    }
    if (it.kind === KIND.TASK) return it.flags & FLAG.ANYTIME ? 'anytime' : daysText(it.days) + ' · ' + minToHHMM(it.at);
    return dayText(it.eventDay) + ' ' + minToHHMM(it.at) + ' · ' + ladderName(it.ladder);
  }

  function csvField(s) {
    s = String(s);
    return /[",\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
  }

  function csvNumbers(logs, tzMin) {
    var rows = ['date,time,item,value,unit'];
    for (var i = 0; i < logs.length; i++) {
      var l = logs[i], at = local(l.t, tzMin);
      rows.push([dayToISO(at.day), minToHHMM(at.min), l.label, fromScaled(l.v, l.dec), l.unit].map(csvField).join(','));
    }
    return rows.join('\n');
  }

  function csvOutcomes(hist) {
    var rows = ['date,item,result,count,missed'];
    for (var i = 0; i < hist.length; i++) {
      for (var j = 0; j < hist[i].out.length; j++) {
        var o = hist[i].out[j];
        rows.push([dayToISO(hist[i].day), o.name, STATUS[o.status] || 'unknown', o.count, o.missed].map(csvField).join(','));
      }
    }
    return rows.join('\n');
  }

  function reasonText(r) {
    if (r === 5) return 'phone not reachable';
    if (r === 6) return 'dictation is off';
    if (r === 4) return 'no speech heard';
    return 'transcription failed';
  }

  function noteHeading(n, tzMin) {
    var at = local(n.id, tzMin);
    return at.wday + ' ' + at.date + ' ' + at.month + ' ' + at.y + ' ' + minToHHMM(at.min) + ' · ' + durText(n.len);
  }

  function textNotes(notes, tzMin) {
    return notes.map(function (n) {
      var head = noteHeading(n, tzMin) + ' · ' + (n.status === 1 ? 'missed: ' + reasonText(n.reason) : n.name || 'Voice note');
      return n.status === 1 ? head : head + '\n' + n.text;
    }).join('\n\n');
  }

  var M = { KIND: KIND, FLAG: FLAG, STEP: STEP, LADDERS: LADDERS, MAX_ITEMS: MAX_ITEMS, MAX_STEPS: MAX_STEPS,
    MAX_RUNGS: MAX_RUNGS, NAME_BYTES: NAME_BYTES, UNIT_BYTES: UNIT_BYTES, STATUS: STATUS,
    utf8Bytes: utf8Bytes, utf8Cut: utf8Cut, newItem: newItem, newStep: newStep, validate: validate, clean: clean,
    ladderName: ladderName, rungText: rungText, minToHHMM: minToHHMM, hhmmToMin: hhmmToMin, isoToDay: isoToDay,
    dayToISO: dayToISO, dayText: dayText, local: local, fromScaled: fromScaled, toScaled: toScaled, durText: durText,
    daysText: daysText, summary: summary, csvNumbers: csvNumbers, csvOutcomes: csvOutcomes, reasonText: reasonText,
    noteHeading: noteHeading, textNotes: textNotes };
  if (typeof module !== 'undefined' && module.exports) module.exports = M;
  else root.CoachModel = M;
})(this);
