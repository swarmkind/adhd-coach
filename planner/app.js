// Coach planner page. Data arrives as window.COACH; Save returns the edited schedule, settings and
// note edits through `return_to` (the emulator tool) or pebblejs://close# (the phone app).
(function () {
  var M = window.CoachModel, K = M.KIND, F = M.FLAG, ST = M.STEP;
  var D = window.COACH || { v: 1, nextId: 1, items: [], settings: {}, notes: [], logs: [], hist: [], now: Date.now() };
  var TODAY = M.local(Math.floor(Date.now() / 1000)).day;
  // What stayed on the phone because the page travels as one URL; an export of it opens as its own page.
  var MORE = D.more || { notes: 0, logs: 0, days: 0 };
  var MORE_OF = { notes: 'notes', numbers: 'logs', days: 'days' };
  var KIND_NAME = ['Routine', 'Reminder', 'Task', 'Event'];
  var KIND_PLURAL = ['Routines', 'Reminders', 'Tasks', 'Events'];
  var SETTINGS = [
    { k: 'laterTask', t: 'Later for tasks', min: 5, max: 120, step: 5, u: 'min' },
    { k: 'laterReminder', t: 'Later for reminders', min: 5, max: 60, step: 5, u: 'min' },
    { k: 'laterEvent', t: 'Later for events', min: 15, max: 240, step: 15, u: 'min' },
    { k: 'eventTime', t: 'Event reminders at', min: 360, max: 720, step: 60, u: 'time' },
    { k: 'focusWork', t: 'Focus', min: 5, max: 90, step: 5, u: 'min', focus: 1 },
    { k: 'focusShort', t: 'Short break', min: 1, max: 30, step: 1, u: 'min', focus: 1 },
    { k: 'focusLong', t: 'Long break', min: 5, max: 60, step: 5, u: 'min', focus: 1 },
    { k: 'focusCycles', t: 'Focus cycles', min: 1, max: 8, step: 1, u: '', focus: 1 }
  ];

  function clone(x) { return JSON.parse(JSON.stringify(x)); }
  var S = { items: clone(D.items || []), settings: clone(D.settings || {}), notes: clone(D.notes || []), ops: [],
    nextId: D.nextId || 1, tab: 'schedule', edit: null, note: null, renaming: null, modal: null, errors: [] };
  var initial = JSON.stringify([S.items, S.settings]);
  function dirty() { return S.ops.length > 0 || JSON.stringify([S.items, S.settings]) !== initial; }

  // ---- Small drawing helpers ----

  function esc(s) {
    return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  var GLYPH = {
    next: 'M9 5l7 7-7 7', up: 'M5 15l7-7 7 7', down: 'M5 9l7 7 7-7', plus: 'M12 4v16M4 12h16', minus: 'M4 12h16',
    close: 'M6 6l12 12M18 6L6 18', back: 'M15 5l-7 7 7 7'
  };
  function glyph(name, size) {
    return '<svg width="' + (size || 24) + '" height="' + (size || 24) + '" viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
      ' stroke-width="3.2" stroke-linecap="square" aria-hidden="true"><path d="' + GLYPH[name] + '"/></svg>';
  }

  // The watch's chunky numerals, drawn from bars: a b c d e f g segments on a 10 x 18 cell.
  var SEGS = { a: [0, 0, 10, 3], b: [7, 0, 3, 10], c: [7, 8, 3, 10], d: [0, 15, 10, 3], e: [0, 8, 3, 10],
    f: [0, 0, 3, 10], g: [0, 7.5, 10, 3] };
  var DIGIT = ['abcdef', 'bc', 'abged', 'abgcd', 'fgbc', 'afgcd', 'afgedc', 'abc', 'abcdefg', 'abcdfg'];
  function seg(text, h) {
    var x = 0, rects = '';
    for (var i = 0; i < text.length; i++) {
      var ch = text.charAt(i);
      if (ch === ':') {
        rects += '<rect x="' + x + '" y="4" width="3" height="3"/><rect x="' + x + '" y="11" width="3" height="3"/>';
        x += 5;
      } else if (ch === '.') {
        rects += '<rect x="' + x + '" y="15" width="3" height="3"/>';
        x += 5;
      } else if (ch === '-') {
        rects += '<rect x="' + x + '" y="7.5" width="8" height="3"/>';
        x += 10;
      } else if (DIGIT[ch]) {
        var on = DIGIT[ch], dx = ch === '1' ? -7 : 0;  // a narrow 1, as on the watch
        for (var j = 0; j < on.length; j++) {
          var r = SEGS[on.charAt(j)];
          rects += '<rect x="' + (x + dx + r[0]) + '" y="' + r[1] + '" width="' + r[2] + '" height="' + r[3] + '"/>';
        }
        x += ch === '1' ? 6 : 13;
      }
    }
    var w = Math.max(1, x - 3);
    return '<svg class="seg" height="' + h + '" width="' + (w * h / 18).toFixed(1) + '" viewBox="0 0 ' + w + ' 18"' +
      ' fill="currentColor" role="img" aria-label="' + esc(text) + '">' + rects + '</svg>';
  }

  // The time is drawn in big numerals beside the name; the line under it says the rest.
  function itemTime(it) {
    if (it.kind === K.REMINDER || (it.kind === K.TASK && (it.flags & F.ANYTIME))) return '';
    return M.minToHHMM(it.at);
  }

  function subLine(it) {
    var p = [], n = (it.steps || []).length;
    if (it.kind === K.ROUTINE) p.push(M.whenText(it), n + (n === 1 ? ' step' : ' steps'));
    if (it.kind === K.REMINDER) {
      p.push('every ' + it.every + ' min', M.minToHHMM(it.at) + ' to ' + M.minToHHMM(it.until));
      if (it.goal) p.push('goal ' + it.goal);
    }
    if (it.kind === K.TASK) p.push(M.whenText(it));
    if (it.kind === K.EVENT) p.push(M.dayText(it.eventDay), M.ladderName(it.ladder));
    if (it.priority) p.push(it.priority === 2 ? 'critical' : 'important');
    if (it.flags & F.SILENT) p.push('silent');
    return p.join(' \u00b7 ');
  }

  // ---- Views ----

  function tabs() {
    var t = [['schedule', 'Schedule'], ['notes', 'Notes'], ['history', 'History'], ['settings', 'Settings']];
    return '<nav class="tabs" role="tablist">' + t.map(function (x) {
      return '<button class="tab" role="tab" data-act="tab" data-v="' + x[0] + '" aria-selected="' + (S.tab === x[0]) + '">' +
        x[1] + '</button>';
    }).join('') + '</nav>';
  }

  function anytimeList() {
    return S.items.filter(function (it) { return it.kind === K.TASK && (it.flags & F.ANYTIME); });
  }

  function slab(it) {
    var rail;
    if (it.kind === K.TASK && (it.flags & F.ANYTIME)) {
      var list = anytimeList(), i = list.indexOf(it);
      rail = '<div class="rail"><button data-act="move" data-id="' + it.id + '" data-v="-1" aria-label="Move up"' +
        (i === 0 ? ' disabled' : '') + '>' + glyph('up', 22) + '</button><button data-act="move" data-id="' + it.id +
        '" data-v="1" aria-label="Move down"' + (i === list.length - 1 ? ' disabled' : '') + '>' + glyph('down', 22) +
        '</button></div>';
    } else {
      rail = '<div class="rail"><button data-act="edit" data-id="' + it.id + '" aria-label="Edit">' + glyph('next') + '</button></div>';
    }
    var t = itemTime(it);
    return '<div class="slab k' + it.kind + '"><button class="field" data-act="edit" data-id="' + it.id + '">' +
      '<span class="name">' + esc(it.name || 'Untitled') + '</span><span class="sub">' + esc(subLine(it)) + '</span>' +
      (t ? seg(t, 22) : '') + '</button>' + rail + '</div>';
  }

  // Limits across the schedule, such as a 1.4.1 schedule with more steps than the pool: Save waits on them.
  function overErrors() {
    var e = M.scheduleErrors(S.items);
    return e.length ? '<div class="errors" role="alert">' + e.map(function (m) { return '<span>' + esc(m) + '</span>'; }).join('') +
      '<span>Remove some steps, then Save.</span></div>' : '';
  }

  function scheduleView() {
    var out = overErrors(), full = S.items.length >= M.MAX_ITEMS;
    for (var k = 0; k < 4; k++) {
      var list = S.items.filter(function (it) { return it.kind === k; });
      if (k === K.TASK) {  // timed tasks by time, then the anytime backlog in its order
        list = list.filter(function (it) { return !(it.flags & F.ANYTIME); }).sort(byTime).concat(anytimeList());
      } else if (k === K.EVENT) {
        list.sort(function (a, b) { return a.eventDay - b.eventDay || a.at - b.at; });
      } else {
        list.sort(byTime);
      }
      out += '<section class="k' + k + '"><h2>' + KIND_PLURAL[k] + ' <span class="n">' + list.length + '</span></h2>' +
        list.map(slab).join('') +
        (full ? '' : '<button class="add" data-act="new" data-v="' + k + '">' + glyph('plus', 20) + 'New ' +
          KIND_NAME[k].toLowerCase() + '</button>') + '</section>';
    }
    if (full) out += '<p class="hint">The watch holds ' + M.MAX_ITEMS + ' things. Delete one to add another.</p>';
    return out;
  }

  function byTime(a, b) { return a.at - b.at; }

  function noteTitle(n) {
    if (n.name) return n.name;
    var at = M.local(n.id);
    return at.wday + ' ' + at.date + ' ' + at.month + ' ' + M.minToHHMM(at.min);
  }

  function notesView() {
    var list = S.notes.slice().sort(function (a, b) { return b.id - a.id; });
    if (!list.length) return '<p class="empty">No voice notes yet. Press Down on the watch home screen to record one.</p>';
    var out = '<div class="export"><button data-act="export" data-v="notes">Export all notes ' + glyph('next', 20) + '</button></div>' +
      '<h2 class="kv">Voice notes <span class="n">' + (list.length + MORE.notes) + '</span></h2>' +
      (MORE.notes ? '<p class="hint">These are the newest ' + list.length + '. The phone keeps ' + MORE.notes + ' older ' +
        (MORE.notes === 1 ? 'one' : 'ones') + ' too, and Export all notes gets every one.</p>' : '');
    return out + list.map(function (n) {
      var open = S.note === n.id, missed = n.status === 1;
      var at = M.local(n.id);
      var meta = (n.name ? at.wday + ' ' + at.date + ' ' + at.month + ' ' + M.minToHHMM(at.min) + ' \u00b7 ' : '') +
        M.durText(n.len) + (missed ? ' \u00b7 missed: ' + M.reasonText(n.reason) : '') + (n.linkName ? ' \u00b7 ' + n.linkName : '');
      var body = '';
      if (open && S.renaming === n.id) {
        body = '<div class="rename"><input data-f="rename" value="' + esc(n.name) + '" maxlength="60" placeholder="Name" aria-label="Note name">' +
          '<button class="go" data-act="rename-ok" data-id="' + n.id + '" style="background:#000;color:var(--kc);padding:0 14px;font-weight:700">Done</button></div>';
      } else if (open) {
        body = (missed ? '' : '<div class="text">' + esc(n.text) + '</div>') +
          '<div class="acts"><button data-act="rename" data-id="' + n.id + '">Rename</button>' +
          '<button data-act="del-note" data-id="' + n.id + '">Delete</button></div>';
      }
      return '<div class="slab note kv' + (missed ? ' quiet' : '') + '"><div class="field">' +
        '<span class="name">' + esc(noteTitle(n)) + '</span><span class="sub">' + esc(meta) + '</span>' + body + '</div>' +
        '<div class="rail"><button data-act="note" data-id="' + n.id + '" aria-label="' + (open ? 'Close' : 'Open') + '">' +
        glyph(open ? 'up' : 'down') + '</button></div></div>';
    }).join('');
  }

  function historyView() {
    var logs = D.logs || [], hist = D.hist || [];
    if (!logs.length && !hist.length) return '<p class="empty">Nothing logged yet. Numbers from routines and reminders, and each day\u2019s results, show up here after the watch syncs.</p>';
    var out = '<div class="export">' +
      (logs.length ? '<button data-act="export" data-v="numbers">Export numbers (CSV) ' + glyph('next', 20) + '</button>' : '') +
      (hist.length ? '<button data-act="export" data-v="days">Export days (CSV) ' + glyph('next', 20) + '</button>' : '') + '</div>' +
      (MORE.logs || MORE.days ? '<p class="hint">Recent numbers and the last ' + hist.length + ' days. The exports hold everything the phone has kept.</p>' : '');
    if (logs.length) {
      out += '<h2>Numbers</h2>' + M.series(logs).map(function (one) {
        var g = M.pairWeights(one), last = g[0], w = last.weight;
        var unit = (last.unit ? last.unit + ' ' : '') + (w ? '\u00d7 ' + M.fromScaled(w.v, w.dec) + (w.unit ? ' ' + w.unit : '') : '');
        return '<div class="series"><div class="top"><span class="label">' + esc(last.label) + '</span>' +
          '<span class="big">' + seg(M.fromScaled(last.v, last.dec), 26) + '<span class="unit">' + esc(unit) + '</span></span></div>' +
          '<ol>' + g.slice(0, 8).map(function (e) {
            var at = M.local(e.t);
            return '<li><span>' + at.wday + ' ' + at.date + ' ' + at.month + ' ' + M.minToHHMM(at.min) + '</span><b>' +
              esc(e.text) + '</b></li>';
          }).join('') + '</ol></div>';
      }).join('');
    }
    if (hist.length) {
      out += '<h2>Days</h2>' + hist.slice().reverse().slice(0, 60).map(function (h) {
        var c = [0, 0, 0, 0, 0, 0];
        h.out.forEach(function (o) { c[o.status] = (c[o.status] || 0) + 1; });
        var parts = [];
        if (c[2]) parts.push(c[2] + ' done');
        if (c[3]) parts.push(c[3] + ' skipped');
        if (c[4]) parts.push(c[4] + ' missed');
        if (!parts.length) parts.push('nothing marked');
        return '<div class="day"><span>' + M.dayText(h.day) + '</span><span class="counts">' + parts.join(' \u00b7 ') + '</span></div>';
      }).join('');
    }
    return out;
  }

  function settingValue(d, v) { return d.u === 'time' ? M.minToHHMM(v) : d.u ? v + ' ' + d.u : v + (v === 1 ? ' cycle' : ' cycles'); }

  function settingsView() {
    var out = '<h2>Alerts</h2>';
    SETTINGS.forEach(function (d, i) {
      if (d.focus && !SETTINGS[i - 1].focus) out += '<h2 class="kf">Focus</h2>';
      var v = S.settings[d.k];
      out += '<div class="row"><span class="label">' + d.t + '</span><span class="stepper">' +
        '<button data-act="set" data-v="' + d.k + '" data-d="-1" aria-label="Less"' + (v <= d.min ? ' disabled' : '') + '>' + glyph('minus', 20) + '</button>' +
        '<output>' + settingValue(d, v) + '</output>' +
        '<button data-act="set" data-v="' + d.k + '" data-d="1" aria-label="More"' + (v >= d.max ? ' disabled' : '') + '>' + glyph('plus', 20) + '</button>' +
        '</span></div>';
    });
    if (D.lastSync) {
      var at = M.local(D.lastSync);
      out += '<p class="hint">Last sync with the watch: ' + at.wday + ' ' + at.date + ' ' + at.month + ' ' + M.minToHHMM(at.min) + '</p>';
    }
    return out;
  }

  // ---- The item editor ----

  function choice(field, options, current) {
    return '<div class="choice">' + options.map(function (o) {
      return '<button data-act="pick" data-f="' + field + '" data-v="' + o[0] + '" aria-pressed="' + (String(current) === String(o[0])) + '">' + o[1] + '</button>';
    }).join('') + '</div>';
  }

  function sw(field, on, label) {
    return '<div class="row"><span class="label">' + label + '</span><button class="switch" data-act="toggle" data-f="' + field +
      '" aria-pressed="' + !!on + '" aria-label="' + label + '"></button></div>';
  }

  function daysPicker(bits) {
    var order = [1, 2, 3, 4, 5, 6, 0], L = ['S', 'M', 'T', 'W', 'T', 'F', 'S'], N = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
    return '<div class="days">' + order.map(function (d) {
      return '<button data-act="day" data-v="' + d + '" aria-pressed="' + !!(bits & (1 << d)) + '" aria-label="' + N[d] + '">' + L[d] + '</button>';
    }).join('') + '</div>';
  }

  function numFields(prefix, n) {
    return '<div class="grid2">' +
      '<label>Unit<input class="txt" data-f="' + prefix + '.unit" value="' + esc(n.unit) + '" maxlength="5" placeholder="kg"></label>' +
      '<label>Decimals<select data-f="' + prefix + '.decimals">' + [0, 1, 2].map(function (d) {
        return '<option value="' + d + '"' + (n.decimals === d ? ' selected' : '') + '>' + d + '</option>';
      }).join('') + '</select></label>' +
      '<label>Lowest<input class="txt" inputmode="decimal" data-f="' + prefix + '.min" value="' + M.fromScaled(n.min, n.decimals) + '"></label>' +
      '<label>Highest<input class="txt" inputmode="decimal" data-f="' + prefix + '.max" value="' + M.fromScaled(n.max, n.decimals) + '"></label>' +
      '<label>Step<input class="txt" inputmode="decimal" data-f="' + prefix + '.step" value="' + M.fromScaled(n.step, n.decimals) + '"></label>' +
      '</div>';
  }

  // Step-ups are stored in days and shown in weeks when they come in whole weeks.
  var PER = [['w', 'weeks'], ['d', 'days']];
  function perOf(path, r) { return S.edit.per[path] || (r.every && r.every % 7 ? 'd' : 'w'); }

  function todayField(path, r, dec, unit) {
    return '<label>Today' + (unit ? ' (' + esc(unit) + ')' : '') + '<input class="txt" inputmode="decimal" data-f="' + path +
      '.today" value="' + M.fromScaled(M.valueAt(r, TODAY), dec) + '"></label>';
  }

  // Today's value (`today`), then Increase, Every n weeks or days, and Stop at; values scaled by `dec`.
  function rampFields(path, r, dec, today, unit) {
    var per = perOf(path, r), n = r.every ? (per === 'w' ? r.every / 7 : r.every) : '', u = unit ? ' (' + esc(unit) + ')' : '';
    return '<div class="grid2">' + today +
      '<label>Increase' + u + '<input class="txt" inputmode="decimal" data-f="' + path + '.inc" value="' +
      (r.inc ? M.fromScaled(r.inc, dec) : '') + '" placeholder="none"></label>' +
      '<label>Every<span class="per"><input class="txt" inputmode="numeric" data-f="' + path + '.every" value="' + n +
      '" aria-label="Every how many"><select data-f="' + path + '.per" aria-label="Weeks or days">' + PER.map(function (o) {
        return '<option value="' + o[0] + '"' + (o[0] === per ? ' selected' : '') + '>' + o[1] + '</option>';
      }).join('') + '</select></span></label>' +
      '<label>Stop at' + u + '<input class="txt" inputmode="decimal" data-f="' + path + '.cap" value="' +
      (r.cap ? M.fromScaled(r.cap, dec) : '') + '" placeholder="none"></label></div>';
  }

  // What a step or its alternative (data path `p`) is and counts.
  function stepFields(p, s) {
    var out = choice(p + '.type', [[ST.MANUAL, 'Do it'], [ST.TIMED, 'Timer'], [ST.NUMBER, 'Number']], s.type);
    if (s.type === ST.TIMED) {
      var len = M.valueAt(s.amount, TODAY);
      out += rampFields(p + '.amount', s.amount, 0,
        '<label>Minutes<input class="txt" inputmode="numeric" data-f="' + p + '.amount.min" value="' + Math.floor(len / 60) + '"></label>' +
        '<label>Seconds<input class="txt" inputmode="numeric" data-f="' + p + '.amount.sec" value="' + len % 60 + '"></label>', 's');
    } else if (s.type === ST.NUMBER) {
      out += numFields(p + '.num', s.num);
    } else {
      var q = s.flags & M.SFLAG.QTY, w = s.flags & M.SFLAG.WEIGHT;
      out += sw(p + '.qty', q, 'Quantity');
      if (q) out += rampFields(p + '.amount', s.amount, 0, todayField(p + '.amount', s.amount, 0, ''), '');
      out += sw(p + '.weight', w, 'Weight');
      if (w) {
        out += '<div class="grid2"><label>Unit<input class="txt" data-f="' + p + '.num.unit" value="' + esc(s.num.unit) +
          '" maxlength="5" placeholder="kg"></label><label>Decimals<select data-f="' + p + '.num.decimals">' + [0, 1, 2].map(function (d) {
            return '<option value="' + d + '"' + (s.num.decimals === d ? ' selected' : '') + '>' + d + '</option>';
          }).join('') + '</select></label></div>' +
          rampFields(p + '.weight', s.weight, s.num.decimals, todayField(p + '.weight', s.weight, s.num.decimals, s.num.unit), s.num.unit);
      }
    }
    return out;
  }

  function everyFields(path, r) {
    return '<div class="grid2"><label>Every (days)<input class="txt" inputmode="numeric" data-f="' + path + '.every" value="' + r.every +
      '"></label><label>Starting<input type="date" data-f="' + path + '.since" value="' + M.dayToISO(r.since) + '"></label></div>';
  }

  function stepCard(s, i, n) {
    var p = 'steps.' + i;
    var alt = s.alt ? '<div class="alt"><div class="althead"><span>Or instead</span><button data-act="alt-del" data-v="' + i +
        '">Remove alternative</button></div><input class="txt" data-f="' + p + '.alt.name" value="' + esc(s.alt.name) +
        '" placeholder="Alternative name" aria-label="Step ' + (i + 1) + ' alternative name">' + stepFields(p + '.alt', s.alt) + '</div>'
      : '<button class="add" data-act="alt-add" data-v="' + i + '">' + glyph('plus', 20) + 'Add alternative</button>';
    return '<div class="step"><div class="card"><div class="top">' + seg(String(i + 1), 20) +
      '<input class="txt" data-f="' + p + '.name" value="' + esc(s.name) + '" placeholder="Step name" aria-label="Step ' + (i + 1) + ' name"></div>' +
      stepFields(p, s) + '<span class="lbl">Show</span>' +
      choice(p + '.show', [[0, 'Every time'], [1, 'Every few days']], s.show.every ? 1 : 0) +
      (s.show.every ? everyFields(p + '.show', s.show) : '') + alt + '</div><div class="rail">' +
      '<button data-act="step-move" data-v="' + i + '" data-d="-1" aria-label="Move step up"' + (i === 0 ? ' disabled' : '') + '>' + glyph('up', 20) + '</button>' +
      '<button data-act="step-move" data-v="' + i + '" data-d="1" aria-label="Move step down"' + (i === n - 1 ? ' disabled' : '') + '>' + glyph('down', 20) + '</button>' +
      '<button data-act="step-del" data-v="' + i + '" aria-label="Delete step">' + glyph('close', 20) + '</button></div></div>';
  }

  // Routines and tasks: on weekdays (every day for an anytime task), or every N days from a date.
  function repeatFields(it, anytime) {
    return choice('repeat', [[0, anytime ? 'Every day' : 'On these days'], [1, 'Every few days']], it.repeat.every ? 1 : 0) +
      (it.repeat.every ? everyFields('repeat', it.repeat) : anytime ? '' : daysPicker(it.days));
  }

  function editorView() {
    var e = S.edit, it = e.draft, k = it.kind, out = '';
    var bytes = M.utf8Bytes(it.name);
    out += '<div class="sheet k' + k + '"><div class="head"><div class="field"><div class="kind"><span>' +
      (e.isNew ? 'New ' + KIND_NAME[k].toLowerCase() : KIND_NAME[k]) + '</span><span data-count>' + bytes + '/31</span></div>' +
      '<input data-f="name" value="' + esc(it.name) + '" placeholder="Name" aria-label="Name" autocomplete="off"></div>' +
      '<div class="rail"><button data-act="edit-cancel" aria-label="Back without keeping changes">' + glyph('close') + '</button></div></div><div class="body">';
    var anytime = k === K.TASK && (it.flags & F.ANYTIME);
    if (k === K.TASK) out += sw('anytime', anytime, 'Anytime (a backlog, no set time)');
    if (anytime) out += '<h2>Repeat</h2>' + repeatFields(it, true);
    if (k === K.EVENT) {
      out += '<h2>When</h2><div class="row"><label for="f-date">Date</label><input id="f-date" type="date" data-f="date" value="' +
        M.dayToISO(it.eventDay) + '"></div><div class="row"><label for="f-at">Time</label><input id="f-at" type="time" data-f="at" value="' +
        M.minToHHMM(it.at) + '"></div><h2>Reminders</h2>' +
        choice('ladder', [['gentle', 'Gentle'], ['normal', 'Normal'], ['persistent', 'Persistent'], ['custom', 'Custom']], e.ladderMode || M.ladderName(it.ladder));
      if ((e.ladderMode || M.ladderName(it.ladder)) === 'custom') {
        out += '<div class="choice" style="margin-top:8px">' + [21, 14, 7, 3, 2, 1, 0, -1].map(function (r) {
          return '<button data-act="rung" data-v="' + r + '" aria-pressed="' + (it.ladder.indexOf(r) >= 0) + '">' + M.rungText(r) + '</button>';
        }).join('') + '</div>';
      }
      var rungs = it.ladder.slice().sort(function (a, b) { return b - a; }).map(M.rungText).join(', ');
      out += '<p class="hint">' + (rungs ? rungs.charAt(0).toUpperCase() + rungs.slice(1) : 'No reminders') +
        '. Advance reminders come at ' + M.minToHHMM(S.settings.eventTime || 540) + '.</p>';
    } else if (!anytime) {
      out += '<h2>When</h2>' + (k === K.REMINDER ? daysPicker(it.days) : repeatFields(it, false));
      if (k === K.REMINDER) {
        out += '<div class="row"><label for="f-at">From</label><input id="f-at" type="time" data-f="at" value="' + M.minToHHMM(it.at) + '"></div>' +
          '<div class="row"><label for="f-until">Until</label><input id="f-until" type="time" data-f="until" value="' + M.minToHHMM(it.until) + '"></div>' +
          '<div class="row"><label for="f-every">Every (minutes)</label><input id="f-every" class="txt short" inputmode="numeric" data-f="every" value="' + it.every + '"></div>' +
          '<div class="row"><label for="f-goal">Daily goal (0 = none)</label><input id="f-goal" class="txt short" inputmode="numeric" data-f="goal" value="' + it.goal + '"></div>';
      } else {
        out += '<div class="row"><label for="f-at">Time</label><input id="f-at" type="time" data-f="at" value="' + M.minToHHMM(it.at) + '"></div>' +
          '<div class="row"><label for="f-grace">Fine to start late by</label><select id="f-grace" data-f="grace">' +
          [0, 5, 10, 15, 30, 60, 120].map(function (g) {
            return '<option value="' + g + '"' + (it.grace === g ? ' selected' : '') + '>' + (g ? g + ' min' : 'no grace') + '</option>';
          }).join('') + '</select></div>';
      }
    }
    out += '<h2>Alert</h2>' + choice('priority', [[0, 'Normal'], [1, 'Important'], [2, 'Critical']], it.priority) +
      sw('silent', it.flags & F.SILENT, 'Silent (shows in Today, never buzzes)');
    if (k === K.REMINDER) {
      out += sw('ask', it.flags & F.ASK, 'Ask for a number each time');
      if (it.flags & F.ASK) out += numFields('num', it.num);
    }
    if (k === K.ROUTINE) {
      out += '<h2>Steps <span class="n">' + it.steps.length + ' of ' + M.MAX_STEPS + '</span></h2>' +
        it.steps.map(function (s, i) { return stepCard(s, i, it.steps.length); }).join('') +
        (it.steps.length < M.MAX_STEPS ? '<button class="add" data-act="step-add">' + glyph('plus', 20) + 'Add a step</button>' : '') +
        M.warnings(it).map(function (w) { return '<p class="hint warn">' + esc(w) + '</p>'; }).join('');
    }
    if (S.errors.length) S.errors = editErrors(it);  // shown since Done: a fix clears its message straight away
    if (S.errors.length) out += '<div class="errors" role="alert">' + errorSpans(S.errors) + '</div>';
    out += '</div></div>';
    return out;
  }

  function bar() {
    if (S.edit) {
      return '<div class="bar">' + (S.edit.isNew ? '<span></span>' : '<button class="text danger" data-act="del-item">Delete</button>') +
        '<button class="go" data-act="edit-done">Done</button></div>';
    }
    return '<div class="bar"><button class="text" data-act="cancel">Cancel</button><span class="state">' +
      (dirty() ? 'Not saved yet' : 'No changes') + '</span><button class="go" data-act="save">Save</button></div>';
  }

  function modalView() {
    var m = S.modal;
    if (m.kind === 'export') {
      return '<div class="modal"><div class="panel"><h3>' + esc(m.title) + '</h3><textarea readonly data-export>' + esc(m.text) +
        '</textarea><p data-copied>' + esc(m.note || 'Copy it, then paste it wherever you keep things.') + '</p><div class="btns">' +
        '<button data-act="modal-close">Close</button>' + (navigator.share ? '<button data-act="share">Share</button>' : '') +
        '<button class="go" data-act="copy">Copy</button></div></div></div>';
    }
    return '<div class="modal"><div class="panel"><h3>' + esc(m.title) + '</h3>' + (m.text ? '<p>' + esc(m.text) + '</p>' : '') +
      '<div class="btns"><button data-act="modal-close">' + esc(m.no || 'Keep') + '</button><button class="' + (m.safe ? 'go' : 'danger') +
      '" data-act="modal-yes">' + esc(m.yes) + '</button></div></div></div>';
  }

  // One part of an export that was too big for the planner, opened by PebbleKit JS as its own page.
  function exportPage() {
    var next = D.part < D.parts;
    return '<main class="exp"><h2>' + esc(D.title) + (D.parts > 1 ? ' <span class="n">part ' + D.part + ' of ' + D.parts + '</span>' : '') +
      '</h2><textarea readonly data-export>' + esc(D.text) + '</textarea><p class="hint" data-copied>Copy it, then paste it wherever you keep things.' +
      (next ? ' Then open the next part.' : '') + '</p></main>' +
      '<div class="bar"><button class="text" data-act="done">Done</button>' + (navigator.share ? '<button class="text" data-act="share">Share</button>' : '') +
      (next ? '<button class="text" data-act="next-part">Next part</button>' : '') + '<button class="go" data-act="copy">Copy</button></div>';
  }

  var app = document.getElementById('app');
  function render() {
    if (D.mode === 'export') { app.innerHTML = exportPage(); return; }
    var view = S.tab === 'schedule' ? scheduleView() : S.tab === 'notes' ? notesView() : S.tab === 'history' ? historyView() : settingsView();
    app.innerHTML = (S.edit ? editorView() : tabs() + '<main>' + view + '</main>') + bar() + (S.modal ? modalView() : '');
  }

  // ---- Actions ----

  function find(id) {
    for (var i = 0; i < S.items.length; i++) if (S.items[i].id === id) return S.items[i];
    return null;
  }

  function openEditor(it, isNew) {
    S.edit = { draft: M.upgrade(clone(it)), isNew: isNew, ladderMode: null, per: {}, scroll: window.scrollY };
    S.errors = [];
    render();
    window.scrollTo(0, 0);
  }

  function closeEditor() {
    var y = S.edit.scroll;
    S.edit = null;
    S.errors = [];
    render();
    window.scrollTo(0, y);
  }

  function setPath(obj, path, v) {
    var p = path.split('.'), o = obj;
    for (var i = 0; i < p.length - 1; i++) o = o[p[i]];
    o[p[p.length - 1]] = v;
  }

  function getPath(obj, path) {
    var p = path.split('.'), o = obj;
    for (var i = 0; i < p.length; i++) o = o[p[i]];
    return o;
  }

  // Text fields write through as you type; nothing re-renders, so focus stays put.
  function onInput(ev) {
    var f = ev.target.getAttribute('data-f');
    if (!f || !S.edit) return;
    var it = S.edit.draft, v = ev.target.value;
    if (f === 'name') {
      it.name = v;
      var c = app.querySelector('[data-count]');
      if (c) c.textContent = M.utf8Bytes(v) + '/31';
    } else if (/^steps\.\d+(\.alt)?\.name$/.test(f) || /\.unit$/.test(f)) {
      setPath(it, f, v);
    }
    if (!S.errors.length) return;  // shown since Done: update the box in place, keeping the keyboard up
    S.errors = editErrors(it);
    var box = app.querySelector('.sheet .errors');
    if (box && S.errors.length) box.innerHTML = errorSpans(S.errors);
    else if (box) box.parentNode.removeChild(box);
  }

  function onChange(ev) {
    var f = ev.target.getAttribute('data-f');
    if (!f || !S.edit) return;
    var it = S.edit.draft, v = ev.target.value, m;
    if (f === 'at' || f === 'until') { var t = M.hhmmToMin(v); if (t !== null) it[f] = t; }
    else if (f === 'date') { var d = M.isoToDay(v); if (d !== null) it.eventDay = d; }
    else if (f === 'every' || f === 'goal') { var n = parseInt(v, 10); if (n >= 0) it[f] = Math.min(f === 'goal' ? 255 : 1439, n); }
    else if (f === 'grace') it.grace = parseInt(v, 10) || 0;
    else if ((m = /^(steps\.\d+(?:\.alt)?\.(?:amount|weight))\.(today|min|sec|inc|every|per|cap)$/.exec(f))) {
      if (!rampChange(it, m[1], m[2], v)) return;
    } else if ((m = /^(steps\.\d+\.show|repeat)\.(every|since)$/.exec(f))) {
      var r = getPath(it, m[1]), x = m[2] === 'every' ? parseInt(v, 10) : M.isoToDay(v);
      if (x === null || isNaN(x)) return;
      r[m[2]] = x;
    } else if ((m = /^(.*num)\.(decimals)$/.exec(f))) {
      M.setDecimals(getPath(it, m[1]), weightOf(it, m[1]), parseInt(v, 10));
    } else if ((m = /^(.*num)\.(min|max|step)$/.exec(f))) {
      var sp = getPath(it, m[1]), need = M.decimalsFor(v);
      if (need > sp.decimals) M.setDecimals(sp, weightOf(it, m[1]), need);  // 82.5 with Decimals 0 keeps its half
      var val = M.toScaled(v, sp.decimals);
      if (val !== null) sp[m[2]] = val;
    } else {
      onInput(ev);
      return;
    }
    render();
  }

  function errorSpans(list) { return list.map(function (m) { return '<span>' + esc(m) + '</span>'; }).join(''); }

  // What Done refuses: the item's own problems, and the schedule's limits with it in.
  function editErrors(it) {
    var cleaned = M.clean(it);
    return M.validate(cleaned).concat(M.scheduleErrors(S.items.filter(function (x) { return x.id !== cleaned.id; }).concat([cleaned])));
  }

  // The weight a number's decimals scale: a step's, or none for a reminder's number.
  function weightOf(it, numPath) { return numPath === 'num' ? null : getPath(it, numPath.slice(0, -4)).weight; }

  // An edit to a step-up: today's value starts from today, a new increase or period keeps today's value.
  function rampChange(it, path, key, v) {
    var spec = /weight$/.test(path) ? getPath(it, path.slice(0, -7)).num : null, need = M.decimalsFor(v), x;
    if (spec && /^(today|inc|cap)$/.test(key) && need > spec.decimals) M.setDecimals(spec, getPath(it, path), need);  // 2.5 kg stays 2.5
    var r = getPath(it, path), dec = spec ? spec.decimals : 0;
    if (key === 'min' || key === 'sec') {
      var len = M.valueAt(r, TODAY), n = Math.max(0, parseInt(v, 10) || 0);
      var mins = key === 'min' ? n : Math.floor(len / 60), secs = key === 'sec' ? Math.min(59, n) : len % 60;
      r = M.setToday(r, Math.min(65535, mins * 60 + secs), TODAY);
    } else if (key === 'today') {
      if ((x = M.toScaled(v, dec)) === null) return false;
      r = M.setToday(r, x, TODAY);
    } else if (key === 'inc') {
      if ((x = v.trim() ? M.toScaled(v, dec) : 0) === null) return false;
      r = M.rebase(r, x, r.every || (x ? 7 : 0), TODAY);  // a new increase starts weekly
    } else if (key === 'every' || key === 'per') {
      var was = perOf(path, r), per = key === 'per' ? v : was;
      var k = key === 'every' ? Math.max(0, parseInt(v, 10) || 0) : r.every ? (was === 'w' ? r.every / 7 : r.every) : 0;
      S.edit.per[path] = per;
      r = M.rebase(r, r.inc, per === 'w' ? k * 7 : k, TODAY);
    } else {
      if ((x = v.trim() ? M.toScaled(v, dec) : 0) === null) return false;
      r = M.setCap(r, x, TODAY);
    }
    setPath(it, path, r);
    return true;
  }

  function onClick(ev) {
    var b = ev.target.closest('[data-act]');
    if (!b || b.disabled) return;
    var act = b.getAttribute('data-act'), v = b.getAttribute('data-v'), id = +b.getAttribute('data-id');
    var it = S.edit && S.edit.draft;
    switch (act) {
      case 'tab': S.tab = v; S.note = null; S.renaming = null; render(); window.scrollTo(0, 0); return;
      case 'edit': openEditor(find(id), false); return;
      case 'new': {
        var n = M.newItem(+v, S.nextId++, TODAY);
        if (n.kind === K.ROUTINE) n.steps.push(M.newStep(ST.MANUAL, TODAY));
        openEditor(n, true);
        return;
      }
      case 'move': {
        var list = anytimeList(), a = find(id), j = list.indexOf(a) + +v, other = list[j];
        if (!other) return;
        var ia = S.items.indexOf(a), ib = S.items.indexOf(other);
        S.items[ia] = other; S.items[ib] = a;
        render();
        return;
      }
      case 'pick': {
        var f = b.getAttribute('data-f'), m;
        if (f === 'ladder') {
          S.edit.ladderMode = v;
          if (v !== 'custom') it.ladder = M.LADDERS[v].slice();
        } else if ((m = /^(steps\.\d+(?:\.alt)?)\.type$/.exec(f))) {
          var s = getPath(it, m[1]), fresh = M.newStep(+v, TODAY);
          if (s.type !== +v) {
            s.type = +v; s.flags = 0; s.amount = fresh.amount; s.weight = fresh.weight;
            s.num = s.type === ST.NUMBER && s.num.step ? s.num : fresh.num;
          }
        } else if ((m = /^steps\.(\d+)\.show$/.exec(f))) {
          var sh = it.steps[+m[1]].show;
          if (+v && !sh.every) { sh.every = 2; sh.since = TODAY; }
          if (!+v) sh.every = 0;
        } else if (f === 'repeat') {
          if (+v && !it.repeat.every) it.repeat = { every: 2, since: TODAY };
          if (!+v) it.repeat.every = 0;
        } else {
          it[f] = +v;
        }
        break;
      }
      case 'toggle': {
        var tf = b.getAttribute('data-f'), bit = tf === 'silent' ? F.SILENT : tf === 'anytime' ? F.ANYTIME : F.ASK, tm;
        if ((tm = /^(steps\.\d+(?:\.alt)?)\.(qty|weight)$/.exec(tf))) {  // what a Do it step counts
          var ts = getPath(it, tm[1]), sb = tm[2] === 'qty' ? M.SFLAG.QTY : M.SFLAG.WEIGHT;
          ts.flags ^= sb;
          if ((ts.flags & sb) && sb === M.SFLAG.QTY && !ts.amount.start) ts.amount = M.setToday(ts.amount, 10, TODAY);
          if ((ts.flags & sb) && sb === M.SFLAG.WEIGHT && !ts.num.unit) ts.num = { step: 0, min: 0, max: 0, decimals: 1, unit: 'kg' };
          break;
        }
        it.flags ^= bit;
        if (bit === F.ASK && (it.flags & F.ASK) && !it.num.step) it.num = M.newStep(ST.NUMBER).num;
        break;
      }
      case 'day': it.days ^= 1 << +v; break;
      case 'rung': {
        var r = +v, at = it.ladder.indexOf(r);
        if (at >= 0) it.ladder.splice(at, 1);
        else if (it.ladder.length < M.MAX_RUNGS) it.ladder.push(r);
        it.ladder.sort(function (x, y) { return y - x; });
        break;
      }
      case 'step-add': it.steps.push(M.newStep(ST.MANUAL, TODAY)); break;
      case 'alt-add': {
        var na = M.newStep(ST.MANUAL, TODAY);
        delete na.show; delete na.alt;
        it.steps[+v].alt = na;
        break;
      }
      case 'alt-del': it.steps[+v].alt = null; break;
      case 'step-del': it.steps.splice(+v, 1); break;
      case 'step-move': {
        var i = +v, k = i + +b.getAttribute('data-d'), tmp = it.steps[i];
        it.steps[i] = it.steps[k]; it.steps[k] = tmp;
        break;
      }
      case 'edit-cancel': closeEditor(); return;
      case 'edit-done': {
        var cleaned = M.clean(it);
        S.errors = editErrors(it);
        if (S.errors.length) { render(); var er = app.querySelector('.errors'); if (er) er.scrollIntoView(); return; }
        var old = find(cleaned.id);
        if (old) S.items[S.items.indexOf(old)] = cleaned;
        else S.items.push(cleaned);
        closeEditor();
        return;
      }
      case 'del-item':
        S.modal = { kind: 'confirm', title: 'Delete \u201c' + (it.name || 'this') + '\u201d?', text: 'It leaves the watch at the next sync.',
          yes: 'Delete', run: function () { S.items = S.items.filter(function (x) { return x.id !== it.id; }); closeEditor(); } };
        break;
      case 'note': S.note = S.note === id ? null : id; S.renaming = null; break;
      case 'rename': S.renaming = id; render(); var ri = app.querySelector('[data-f="rename"]'); if (ri) ri.focus(); return;
      case 'rename-ok': {
        var input = app.querySelector('[data-f="rename"]'), note = noteById(id), name = M.utf8Cut(input.value.trim(), 31);
        if (note && name && name !== note.name) { note.name = name; S.ops.push({ op: 'rename', id: id, name: name }); }
        S.renaming = null;
        break;
      }
      case 'del-note': {
        var dn = noteById(id);
        S.modal = { kind: 'confirm', title: 'Delete this note?', text: noteTitle(dn) + '. It goes from the phone and the watch.', yes: 'Delete',
          run: function () { S.notes = S.notes.filter(function (x) { return x.id !== id; }); S.ops.push({ op: 'delete', id: id }); S.note = null; } };
        break;
      }
      case 'set': {
        var d = SETTINGS.filter(function (x) { return x.k === v; })[0], cur = S.settings[v];
        S.settings[v] = Math.max(d.min, Math.min(d.max, cur + d.step * +b.getAttribute('data-d')));
        break;
      }
      case 'export':
        if (!MORE[MORE_OF[v]]) { S.modal = exportFor(v); break; }
        if (!dirty()) { close({ v: 1, export: v }); return; }
        S.modal = { kind: 'confirm', title: 'Save your changes first?', text: 'The full export opens as a page of its own, so the planner saves and closes.',
          yes: 'Save and export', no: 'Stay', safe: true, run: function () { var r = result(); r.export = v; close(r); } };
        break;
      case 'copy': copyExport(); return;
      case 'share': navigator.share({ title: exportTitle(), text: exportText() }).catch(function () {}); return;
      case 'next-part': close({ v: 1, export: D.kind, part: D.part + 1 }); return;
      case 'done': close(null); return;
      case 'modal-close': S.modal = null; break;
      case 'modal-yes': { var run = S.modal.run; S.modal = null; run(); render(); return; }
      case 'cancel':
        if (dirty()) {
          S.modal = { kind: 'confirm', title: 'Leave without saving?', text: 'Your changes stay off the watch.', yes: 'Leave', no: 'Stay',
            run: function () { close(null); } };
          break;
        }
        close(null);
        return;
      case 'save':
        if (M.scheduleErrors(S.items).length) { S.tab = 'schedule'; render(); window.scrollTo(0, 0); return; }  // the watch would refuse it
        close(result()); return;
    }
    render();
  }

  function noteById(id) {
    for (var i = 0; i < S.notes.length; i++) if (S.notes[i].id === id) return S.notes[i];
    return null;
  }

  function exportFor(what) {
    if (what === 'notes') {
      var notes = S.notes.slice().sort(function (a, b) { return a.id - b.id; });
      return { kind: 'export', title: 'Voice notes', text: M.textNotes(notes) };
    }
    if (what === 'numbers') return { kind: 'export', title: 'Numbers (CSV)', text: M.csvNumbers(D.logs || []) };
    return { kind: 'export', title: 'Days (CSV)', text: M.csvOutcomes(D.hist || []) };
  }

  function exportTitle() { return S.modal ? S.modal.title : D.title; }
  function exportText() { return S.modal ? S.modal.text : D.text; }

  function copyExport() {
    var ta = app.querySelector('[data-export]'), p = app.querySelector('[data-copied]');
    function done(ok) { if (p) p.textContent = ok ? 'Copied.' : 'Select the text and copy it by hand.'; }
    ta.focus();
    ta.select();
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(exportText()).then(function () { done(true); }, function () { done(document.execCommand('copy')); });
    } else {
      done(document.execCommand('copy'));
    }
  }

  function result() {
    var items = S.items.map(function (it, i) { var c = M.clean(it); c.order = i; return c; });
    return { v: 1, nextId: S.nextId, items: items, settings: S.settings, ops: S.ops };
  }

  function close(payload) {
    var m = /[?&]return_to=([^&]*)/.exec(location.search);
    var base = m ? decodeURIComponent(m[1]) : 'pebblejs://close#';
    location.href = base + (payload ? encodeURIComponent(JSON.stringify(payload)) : '');
  }

  app.addEventListener('click', onClick);
  app.addEventListener('input', onInput);
  app.addEventListener('change', onChange);
  window.CoachPlanner = { state: S, result: result };  // for the headless walk
  render();
})();
