// Shared test fixtures: an in-memory localStorage and a small schedule.
function MemStorage() { this.m = {}; }
MemStorage.prototype.getItem = function (k) { return Object.prototype.hasOwnProperty.call(this.m, k) ? this.m[k] : null; };
MemStorage.prototype.setItem = function (k, v) { this.m[k] = String(v); };
MemStorage.prototype.removeItem = function (k) { delete this.m[k]; };

function num(step, min, max, decimals, unit) { return { step: step, min: min, max: max, decimals: decimals, unit: unit }; }
var NONE = num(0, 0, 0, 0, '');

function sampleItems() {
  return [
    { id: 1, kind: 0, priority: 0, flags: 0, days: 62, at: 420, until: 0, every: 0, goal: 0, grace: 0, order: 0,
      eventDay: 0, ladder: [], num: NONE, name: 'Morning routine', steps: [
        { type: 0, dur: 0, num: NONE, name: 'Drink a glass of water' },
        { type: 1, dur: 60, num: NONE, name: 'Plank' },
        { type: 2, dur: 0, num: num(1, 300, 2500, 1, 'kg'), name: 'Weight' }] },
    { id: 2, kind: 1, priority: 0, flags: 4, days: 127, at: 540, until: 1260, every: 60, goal: 8, grace: 0, order: 1,
      eventDay: 0, ladder: [], num: num(1, 0, 500, 0, 'reps'), name: 'Drink water', steps: [] },
    { id: 6, kind: 3, priority: 1, flags: 0, days: 0, at: 630, until: 0, every: 0, goal: 0, grace: 0, order: 2,
      eventDay: 20406, ladder: [7, 1, 0], num: NONE, name: 'Dentist check-up', steps: [] }
  ];
}

function clone(x) { return JSON.parse(JSON.stringify(x)); }

// Heavy use straight into a store: per day 3 notes of `text`, 8 number logs and 10 outcomes (the store keeps 365 days).
function yearsOfUse(st, years, text) {
  var t0 = 1761200000, day0 = 20384;
  for (var d = 0; d < Math.round(365 * years); d++) {
    for (var k = 0; k < 8; k++) {
      st.logs.push({ t: t0 + d * 86400 + k * 600, item: 1, step: 2, v: 800 + k, label: 'Morning routine \u00b7 Weight', unit: 'kg', dec: 1 });
    }
    for (var m = 0; m < 3; m++) {
      st.notes.push({ id: t0 + d * 86400 + m * 3600, len: 20, link: 0, linkName: '', status: 0, reason: 0, name: '', text: text });
    }
    var out = [];
    for (var o = 1; o <= 10; o++) out.push({ id: o, status: 2, count: 0, missed: 0, name: 'Thing ' + o, kind: 2 });
    st.hist.push({ day: day0 + d, out: out });
  }
  if (st.hist.length > 365) st.hist = st.hist.slice(st.hist.length - 365);
}

module.exports = { MemStorage: MemStorage, sampleItems: sampleItems, clone: clone, NONE: NONE, num: num, yearsOfUse: yearsOfUse };
