#include "cc_fires.h"
#include "cc_sched.h"

#define SOON 30  // wakeups can't be closer than this

typedef bool (*Before)(const CcFire *a, const CcFire *b);

static bool by_time(const CcFire *a, const CcFire *b) {
  return a->at != b->at ? a->at < b->at : a->priority > b->priority;
}
static bool by_urgency(const CcFire *a, const CcFire *b) {
  return a->priority != b->priority ? a->priority > b->priority : a->at < b->at;
}

// Insert into a sorted array of *n (cap max), dropping whatever sorts last when full.
static void insert(CcFire out[], int *n, int max, CcFire f, Before before) {
  int i;
  if (*n < max) i = (*n)++;
  else if (max > 0 && before(&f, &out[max - 1])) i = max - 1;
  else return;
  while (i > 0 && before(&f, &out[i - 1])) { out[i] = out[i - 1]; i--; }
  out[i] = f;
}

static const CcItemState *state_in(const CcDay *d, uint16_t id) {
  for (int i = 0; i < d->n; i++)
    if (d->s[i].id == id) return &d->s[i];
  return NULL;
}

int cc_collect(const CcItem items[], int n, const CcDay *d, cc_sec step_end, cc_sec focus_end,
               cc_sec now, const CcSettings *cfg, CcFire out[], int max) {
  int k = 0;
  if (step_end != CC_NEVER) insert(out, &k, max, (CcFire){ step_end, 0, CC_FIRE_STEP_END, CC_CRITICAL }, by_time);
  if (focus_end != CC_NEVER) insert(out, &k, max, (CcFire){ focus_end, 0, CC_FIRE_FOCUS_END, CC_CRITICAL }, by_time);
  for (int i = 0; i < n; i++) {
    const CcItem *it = &items[i];
    const CcItemState *s = state_in(d, it->id);
    cc_sec due = cc_next_due(it, s, now, cfg);
    if (due != CC_NEVER) insert(out, &k, max, (CcFire){ due, it->id, CC_FIRE_DUE, it->priority }, by_time);
    cc_sec hu = cc_next_headsup(it, s, now, cfg);
    if (hu != CC_NEVER) insert(out, &k, max, (CcFire){ hu, it->id, CC_FIRE_HEADSUP, it->priority }, by_time);
  }
  return k;
}

int cc_fire_insert(CcFire out[], int n, int max, CcFire f) {
  insert(out, &n, max, f, by_time);
  return n;
}

int cc_due_now(const CcFire f[], int n, cc_sec now, CcFire out[], int max) {
  int k = 0;
  for (int i = 0; i < n; i++)
    if (f[i].at <= now + SOON) insert(out, &k, max, f[i], by_urgency);
  return k;
}

int cc_plan_wakeups(const CcFire f[], int n, cc_sec now, cc_sec out[], int max) {
  int k = 0;
  for (int i = 0; i < n && k < max; i++) {
    cc_sec t = f[i].at < now + SOON ? now + SOON : f[i].at;
    if (k && t - out[k - 1] < 60) continue;
    out[k++] = t;
  }
  return k;
}
