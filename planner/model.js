// The planner's pure model: item defaults, validation against what the watch accepts,
// unit conversions, and the export formats. Runs in the page and in node tests.
(function (root) {
  var KIND = { ROUTINE: 0, REMINDER: 1, TASK: 2, EVENT: 3 };
  var FLAG = { SILENT: 1, ANYTIME: 2, ASK: 4 };
  var STEP = { MANUAL: 0, TIMED: 1, NUMBER: 2 };
  var SFLAG = { QTY: 1, WEIGHT: 2 };  // what a Do it step counts
  var LADDERS = { gentle: [1, 0], normal: [7, 1, 0], persistent: [21, 7, 3, 1, 0, -1] };
  var MAX_ITEMS = 40, MAX_STEPS = 20, MAX_RUNGS = 6, NAME_BYTES = 31, UNIT_BYTES = 5;
  var POOL_MAX = 240, EVERY_MAX = 30;
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

  // A value that steps up: `start` on day `since`, then + `inc` every `every` days, stopping at `cap` (0 = none).
  function ramp(start, since) { return { start: start || 0, cap: 0, since: since || 0, inc: 0, every: 0 }; }
  function copyRamp(r) { return { start: r.start, cap: r.cap, since: r.since, inc: r.inc, every: r.every }; }

  // The watch's cc_ramp_at.
  function valueAt(r, day) {
    if (!r.inc || !r.every || day <= r.since) return r.start;
    var v = r.start + r.inc * Math.floor((day - r.since) / r.every);
    return r.cap && r.cap >= r.start && v > r.cap ? r.cap : v;
  }

  function setToday(r, value, today) { var out = copyRamp(r); out.start = value; out.since = today; return out; }

  // A new increase or period from today's value, counted from the last step-up so the next one stays where
  // it was; never so far back that today would already step up again. A value that didn't step counts from today.
  function rebase(r, inc, every, today) {
    var stepped = r.inc && r.every;
    var last = !stepped ? today : today > r.since ? r.since + Math.floor((today - r.since) / r.every) * r.every : r.since;
    if (inc && every > 0 && last <= today - every) last = today - every + 1;
    return { start: valueAt(r, today), cap: r.cap, since: last, inc: inc, every: every };
  }

  // A new Stop at keeps today's value and the next step-up: the ramp restarts from today's value first,
  // so a value held at the old cap climbs on from there, and a cap below it fails "below today's value".
  function setCap(r, cap, today) {
    var out = rebase(r, r.inc, r.every, today);
    out.cap = cap;
    return out;
  }

  // A step's alternative is a step without `show` and `alt` of its own.
  function newStep(type, today) {
    return { type: type, flags: 0, name: '', num: type === STEP.NUMBER ? num(1, 0, 1000, 0, '') : NO_NUM,
      amount: ramp(type === STEP.TIMED ? 60 : 0, today), weight: ramp(0, today), show: { every: 0, since: today || 0 },
      alt: null };
  }

  function upgradeStep(s, main) {
    var out = { type: s.type, flags: s.flags || 0, name: s.name || '', num: s.num || NO_NUM,
      amount: s.amount ? copyRamp(s.amount) : ramp(s.dur), weight: s.weight ? copyRamp(s.weight) : ramp() };
    if (main) {
      out.show = s.show ? { every: s.show.every, since: s.show.since } : { every: 0, since: 0 };
      out.alt = s.alt ? upgradeStep(s.alt, false) : null;
    }
    return out;
  }

  // A stored 1.4.1 item in the 1.5.0 shape: timer lengths become ramps, no rhythm, no alternatives.
  function upgrade(it) {
    var out = {}, k;
    for (k in it) if (k !== 'steps') out[k] = it[k];
    out.repeat = it.repeat ? { every: it.repeat.every, since: it.repeat.since } : { every: 0, since: 0 };
    out.steps = (it.steps || []).map(function (s) { return upgradeStep(s, true); });
    return out;
  }

  function newItem(kind, id, today) {
    var it = { id: id, kind: kind, priority: 0, flags: 0, days: 0x7F, at: 540, until: 0, every: 0, goal: 0, grace: 0,
      order: 0, eventDay: 0, ladder: [], num: NO_NUM, name: '', steps: [], repeat: { every: 0, since: today || 0 } };
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
    if (timed && !(it.repeat && it.repeat.every) && !(it.days & 0x7F)) e.push('Pick at least one day.');
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
    var rep = it.repeat || {};
    if (rep.every && !(rep.every >= 2 && rep.every <= EVERY_MAX)) e.push('Repeat every 2 to ' + EVERY_MAX + ' days.');
    var steps = it.steps || [];
    if (steps.length > MAX_STEPS) e.push('At most ' + MAX_STEPS + ' steps.');
    for (var i = 0; i < steps.length; i++) {
      var s = steps[i], n = 'Step ' + (i + 1);
      if (!(s.name || '').trim()) e.push(n + ' needs a name.');
      e = e.concat(stepErrors(s, n, n));
      var show = s.show || {};
      if (show.every && !(show.every >= 2 && show.every <= EVERY_MAX)) e.push(n + ': every 2 to ' + EVERY_MAX + ' days.');
      if (s.alt) {
        if (!(s.alt.name || '').trim()) e.push(n + '\'s alternative needs a name.');
        e = e.concat(stepErrors(s.alt, n + '\'s alternative', n));
      }
    }
    return e;
  }

  function rampErrors(r, n) {
    var e = [];
    if (r.inc < 0) e.push(n + ': the increase must be above zero.');
    if (r.inc > 0 && !(r.every >= 1)) e.push(n + ': step up every 1 day or more.');
    if (r.cap && r.cap < r.start) e.push(n + ': stop at is below today\'s value.');
    return e;
  }

  // `who` names the step or its alternative in "needs a length"; `n` starts the other messages.
  function stepErrors(s, who, n) {
    var e = [];
    if (s.type === STEP.TIMED) {
      if (!(s.amount && s.amount.start > 0)) e.push(who + ' needs a length.');
      else e = e.concat(rampErrors(s.amount, n));
    }
    if (s.type === STEP.NUMBER) e = e.concat(numErrors(s.num, n + ': '));
    if (s.type === STEP.MANUAL && (s.flags & SFLAG.QTY)) e = e.concat(rampErrors(s.amount, n));
    if (s.type === STEP.MANUAL && (s.flags & SFLAG.WEIGHT)) e = e.concat(rampErrors(s.weight, n));
    return e;
  }

  function poolSize(it) {
    return (it.steps || []).reduce(function (n, s) { return n + (s.alt ? 2 : 1); }, 0);
  }

  // Limits across the whole schedule.
  function scheduleErrors(items) {
    var n = items.reduce(function (t, it) { return t + poolSize(it); }, 0);
    return n > POOL_MAX ? ['At most ' + POOL_MAX + ' steps across all routines and tasks, alternatives included.'] : [];
  }

  function gcd(a, b) { return b ? gcd(b, a % b) : a; }
  function weekday(day) { return ((day + 4) % 7 + 7) % 7; }  // day 0 was a Thursday; 0 = Sunday

  // A step that shows every N days from `since` meets a routine on its days, or on its own rhythm.
  function meets(it, show) {
    var rep = it.repeat || {};
    if (!show.every) return true;
    if (rep.every) {  // both rhythms meet when their starts agree modulo the gcd of their periods
      var g = gcd(rep.every, show.every);
      return ((show.since - rep.since) % g + g) % g === 0;
    }
    if (show.every % 7) return !!(it.days & 0x7F);  // its days walk through every weekday
    return !!(it.days & (1 << weekday(show.since)));
  }

  // Not errors: a schedule the watch accepts but that can't do what it says.
  function warnings(it) {
    var w = [];
    (it.steps || []).forEach(function (s, i) {
      if (!meets(it, s.show || {})) w.push('Step ' + (i + 1) + ' never falls on a day this routine runs.');
    });
    return w;
  }

  function cleanNum(n) {
    return num(n.step | 0, n.min | 0, n.max | 0, Math.max(0, Math.min(4, n.decimals | 0)), utf8Cut(n.unit, UNIT_BYTES));
  }

  function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v | 0)); }
  function cleanRamp(r) {  // the watch refuses an increase with no period, even on a field that is off
    var every = clamp(r.every, 0, 65535);
    return { start: r.start | 0, cap: r.cap | 0, since: r.since | 0, inc: every ? clamp(r.inc, -32768, 32767) : 0, every: every };
  }
  function cleanStep(s, main) {
    var out = { type: s.type, flags: (s.flags | 0) & (SFLAG.QTY | SFLAG.WEIGHT), num: cleanNum(s.num || NO_NUM),
      amount: cleanRamp(s.amount), weight: cleanRamp(s.weight), name: utf8Cut((s.name || '').trim(), NAME_BYTES) };
    if (out.type === STEP.TIMED) out.amount.start = clamp(out.amount.start, 0, 65535);
    if (main) {
      out.show = { every: clamp(s.show.every, 0, EVERY_MAX), since: s.show.since | 0 };
      out.alt = s.alt ? cleanStep(s.alt, false) : null;
    }
    return out;
  }

  // What the watch stores: names cut on a character boundary, numbers in range.
  function clean(it) {
    var out = upgrade(JSON.parse(JSON.stringify(it)));
    out.name = utf8Cut((out.name || '').trim(), NAME_BYTES);
    out.num = cleanNum(out.num || NO_NUM);
    out.repeat = { every: clamp(out.repeat.every, 0, EVERY_MAX), since: out.repeat.since | 0 };
    out.steps = out.steps.map(function (s) { return cleanStep(s, true); });
    return out;
  }

  // What a routine run logs besides its numbers: each step's outcome (STEP | the step's index, value 1 done
  // or 0 skipped) and its end (RUN_DONE, RUN_SKIP or RUN_LEFT unfinished; value: steps << 25 | steps done << 20 |
  // seconds on its screens).
  var LOG = { STEP: 0x80, RUN_DONE: 0xBF, RUN_SKIP: 0xBE, RUN_LEFT: 0xBD };
  function isOutcome(e) { return e.step !== 255 && (e.step & LOG.STEP) !== 0; }
  function isRunEnd(e) { return e.step === LOG.RUN_DONE || e.step === LOG.RUN_SKIP || e.step === LOG.RUN_LEFT; }

  // History's series, newest first: one per item and label, so a step keeps its own even when the
  // indices move. A weight carries its step's label, so it joins the count it was logged with.
  function series(logs) {
    var groups = {}, order = [];
    for (var i = logs.length - 1; i >= 0; i--) {
      if (isOutcome(logs[i])) continue;
      var key = logs[i].item + '/' + logs[i].label;
      if (!groups[key]) { groups[key] = []; order.push(key); }
      groups[key].push(logs[i]);
    }
    return order.map(function (k) { return groups[k]; });
  }

  // History: each logged value as text, a weight joined to the quantity logged with it (within 2 s).
  function isWeight(e) { return e.step !== 255 && (e.step & 0x40); }
  function valueText(e) { return fromScaled(e.v, e.dec || 0) + (e.unit ? ' ' + e.unit : ''); }
  function pairWeights(entries) {
    var partner = [], taken = [];
    entries.forEach(function (w, i) {
      if (!isWeight(w)) return;
      for (var j = 0; j < entries.length; j++) {
        var q = entries[j];
        if (!taken[j] && !isWeight(q) && q.item === w.item && q.step === (w.step & 0x3F) && Math.abs(q.t - w.t) <= 2) {
          partner[j] = w; taken[j] = taken[i] = true;
          return;
        }
      }
    });
    var out = [];
    entries.forEach(function (e, i) {
      if (isWeight(e) && taken[i]) return;
      var o = {}, k;
      for (k in e) o[k] = e[k];
      o.text = valueText(e) + (partner[i] ? ' \u00d7 ' + valueText(partner[i]) : '');
      if (partner[i]) o.weight = partner[i];
      out.push(o);
    });
    return out;
  }

  // Routine runs from their outcomes, newest first. A run with no end yet is 'open'; one whose outcomes stop
  // before another day began never ended (a watch from before ends were logged) and stands on its own.
  function runs(logs, tzMin) {
    var open = {}, out = [];
    for (var i = 0; i < logs.length; i++) {
      var e = logs[i];
      if (!isOutcome(e)) continue;
      var day = local(e.t, tzMin).day, r = open[e.item];
      if (r && r.day !== day && !isRunEnd(e)) { out.push(r); r = null; }
      if (!r) r = open[e.item] = { item: e.item, name: e.label, day: day, t: e.t, done: 0, of: 0, secs: 0, skipped: [], result: 'open' };
      r.t = e.t;
      if (isRunEnd(e)) {
        r.result = e.step === LOG.RUN_DONE ? 'done' : e.step === LOG.RUN_SKIP ? 'skipped' : 'left';
        r.of = e.v >>> 25 & 31;
        r.done = e.v >>> 20 & 31;  // the watch's count stands, whichever outcomes reached this page
        r.secs = e.v & 0xFFFFF;
        out.push(r);
        delete open[e.item];
      } else if (e.v) {
        r.done++;
      } else {
        r.skipped.push(e.stepName || 'a step');
      }
    }
    for (var k in open) out.push(open[k]);
    return out.sort(function (a, b) { return b.t - a.t; });
  }

  function runMinutes(r) { return r.secs ? Math.max(1, Math.round(r.secs / 60)) : 0; }

  function runText(r) {
    if (r.result === 'open') return r.done + ' done \u00b7 not finished';
    var text = r.done + ' of ' + r.of;
    if (r.result === 'skipped') return 'skipped \u00b7 ' + text;
    if (r.result === 'left') return text + ' \u00b7 not finished';
    return r.secs ? text + ' \u00b7 ' + runMinutes(r) + ' min' : text;
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

  // Decimals a typed number needs: '2.5' needs 1, and so does '2.50'. At most 2, as the planner offers.
  function decimalsFor(s) {
    var m = /\.(\d*?)0*\s*$/.exec(s || '');
    return m && toScaled(s, 2) !== null ? Math.min(2, m[1].length) : 0;
  }

  // New decimals for a number: its range, its step and a step's weight keep their values.
  function setDecimals(spec, weight, nd) {
    var scale = Math.pow(10, nd - spec.decimals);
    spec.min = Math.round(spec.min * scale); spec.max = Math.round(spec.max * scale);
    spec.step = Math.max(1, Math.round(spec.step * scale)); spec.decimals = nd;
    if (weight) { weight.start = Math.round(weight.start * scale); weight.cap = Math.round(weight.cap * scale); weight.inc = Math.round(weight.inc * scale); }
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

  // Which days a routine or task comes up: its rhythm when it has one, else its weekdays.
  function whenText(it) {
    var anytime = it.kind === KIND.TASK && (it.flags & FLAG.ANYTIME), every = it.repeat && it.repeat.every;
    var rhythm = every ? 'every ' + every + (every === 1 ? ' day' : ' days') : '';
    if (anytime) return rhythm ? 'anytime, ' + rhythm : 'anytime';
    return rhythm || daysText(it.days);
  }

  function summary(it) {
    var n = (it.steps || []).length;
    if (it.kind === KIND.ROUTINE) return whenText(it) + ' · ' + minToHHMM(it.at) + ' · ' + n + (n === 1 ? ' step' : ' steps');
    if (it.kind === KIND.REMINDER) {
      return 'every ' + it.every + ' min · ' + minToHHMM(it.at) + ' to ' + minToHHMM(it.until) +
        (it.goal ? ' · goal ' + it.goal : '');
    }
    if (it.kind === KIND.TASK) return whenText(it) + (it.flags & FLAG.ANYTIME ? '' : ' · ' + minToHHMM(it.at));
    return dayText(it.eventDay) + ' ' + minToHHMM(it.at) + ' · ' + ladderName(it.ladder);
  }

  function csvField(s) {
    s = String(s);
    return /[",\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
  }

  function csvNumbers(logs, tzMin) {
    var rows = ['date,time,item,value,unit'];
    for (var i = 0; i < logs.length; i++) {
      if (isOutcome(logs[i])) continue;
      var l = logs[i], at = local(l.t, tzMin);
      rows.push([dayToISO(at.day), minToHHMM(at.min), l.label, fromScaled(l.v, l.dec), l.unit].map(csvField).join(','));
    }
    return rows.join('\n');
  }

  function csvRuns(logs, tzMin) {
    var rows = ['date,time,routine,result,done,steps,minutes,skipped'];
    runs(logs, tzMin).reverse().forEach(function (r) {
      var at = local(r.t, tzMin), open = r.result === 'open';
      rows.push([dayToISO(at.day), minToHHMM(at.min), r.name, open || r.result === 'left' ? 'not finished' : r.result, r.done, open ? '' : r.of,
        runMinutes(r) || '', r.skipped.join('; ')].map(csvField).join(','));
    });
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

  var M = { KIND: KIND, FLAG: FLAG, STEP: STEP, SFLAG: SFLAG, LADDERS: LADDERS, MAX_ITEMS: MAX_ITEMS, MAX_STEPS: MAX_STEPS,
    MAX_RUNGS: MAX_RUNGS, NAME_BYTES: NAME_BYTES, UNIT_BYTES: UNIT_BYTES, POOL_MAX: POOL_MAX, EVERY_MAX: EVERY_MAX,
    STATUS: STATUS, utf8Bytes: utf8Bytes, utf8Cut: utf8Cut, newItem: newItem, newStep: newStep, validate: validate,
    clean: clean, valueAt: valueAt, setToday: setToday, rebase: rebase, setCap: setCap, upgrade: upgrade, scheduleErrors: scheduleErrors,
    warnings: warnings, pairWeights: pairWeights, series: series, LOG: LOG, runs: runs, runText: runText,
    ladderName: ladderName, rungText: rungText, minToHHMM: minToHHMM, hhmmToMin: hhmmToMin, isoToDay: isoToDay,
    dayToISO: dayToISO, dayText: dayText, local: local, fromScaled: fromScaled, toScaled: toScaled, decimalsFor: decimalsFor, setDecimals: setDecimals,
    durText: durText,
    daysText: daysText, whenText: whenText, summary: summary, csvNumbers: csvNumbers, csvRuns: csvRuns, csvOutcomes: csvOutcomes, reasonText: reasonText,
    noteHeading: noteHeading, textNotes: textNotes };
  if (typeof module !== 'undefined' && module.exports) module.exports = M;
  else root.CoachModel = M;
})(this);
