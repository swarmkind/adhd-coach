#include "cc_todo.h"
#include "cc_time.h"
#include <string.h>

#define NAME_LEN (CC_TODO_NAME - 1)

static bool is_space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

// The transcript's start on one line, cut at a word (or, for one long word, a whole letter).
static void make_name(char out[CC_TODO_NAME], const char *text) {
  char b[CC_TODO_NAME + 1];
  int n = 0;
  bool gap = false;
  for (const char *s = text ? text : ""; *s && n <= NAME_LEN; s++) {
    if (is_space(*s)) { gap = n > 0; continue; }
    if (gap) { b[n++] = ' '; gap = false; if (n > NAME_LEN) break; }
    b[n++] = *s;
  }
  if (n > NAME_LEN) {  // b[NAME_LEN] is the first byte that doesn't fit
    n = NAME_LEN;
    while (n > 0 && ((uint8_t)b[n] & 0xC0) == 0x80) n--;
    if (b[n] != ' ') {
      int p = n;
      while (p > 0 && b[p - 1] != ' ') p--;
      if (p - 1 > NAME_LEN / 2) n = p - 1;
    }
  }
  while (n > 0 && b[n - 1] == ' ') n--;
  if (!n) { strcpy(out, "Voice note"); return; }
  memcpy(out, b, n);
  out[n] = 0;
}

CcTodo *cc_todo_for_note(CcTodos *l, uint32_t note) {
  for (int i = 0; i < l->n; i++)
    if (l->t[i].note == note) return &l->t[i];
  return NULL;
}

uint16_t cc_todo_add(CcTodos *l, uint32_t note, const char *text, cc_sec at) {
  CcTodo *had = cc_todo_for_note(l, note);
  if (had) { cc_todo_remind(had, at); return had->id; }
  if (l->n >= CC_TODO_MAX) return 0;
  if (!++l->next_id) l->next_id = 1;
  CcTodo *t = &l->t[l->n++];
  memset(t, 0, sizeof(*t));
  t->id = l->next_id;
  t->note = note;
  t->at = at;
  make_name(t->name, text);
  return t->id;
}

CcTodo *cc_todo_find(CcTodos *l, uint16_t id) {
  for (int i = 0; i < l->n; i++)
    if (l->t[i].id == id) return &l->t[i];
  return NULL;
}

void cc_todo_remind(CcTodo *t, cc_sec at) { t->at = at; t->later_used = 0; }

void cc_todo_answer(CcTodos *l, uint16_t id, CcAction a, cc_sec now, int later_min) {
  CcTodo *t = cc_todo_find(l, id);
  if (!t) return;
  switch (a) {
    case CC_A_DONE: {
      int i = t - l->t;
      memmove(&l->t[i], &l->t[i + 1], (l->n - i - 1) * sizeof(*t));
      l->n--;
      break;
    }
    case CC_A_LATER: t->at = now + later_min * 60; break;
    case CC_A_LATER_LONG: t->at = now + 3600; break;
    case CC_A_IGNORED:
      t->at = t->later_used ? 0 : now + later_min * 60;
      t->later_used = 1;
      break;
    case CC_A_SKIP: case CC_A_PAUSE: t->at = 0; break;
    default: break;
  }
}

int cc_todo_fires(const CcTodos *l, CcFire f[], int n, int max) {
  for (int i = 0; i < l->n; i++)
    if (l->t[i].at) n = cc_fire_insert(f, n, max, (CcFire){ l->t[i].at, l->t[i].id, CC_FIRE_NOTE, CC_NORMAL });
  return n;
}

cc_sec cc_todo_pick(cc_sec at, int field, int dir, cc_sec now) {
  int32_t day = cc_day_of(at);
  int m = cc_min_of(at);
  if (field == CC_PICK_DAY) day += dir;
  else if (field == CC_PICK_HOUR) m = (m + dir * 60 + 1440) % 1440;
  else if (dir) m = m / 60 * 60 + (m % 60 / 5 * 5 + dir * 5 + 60) % 60;
  cc_sec t = cc_at(day, m);
  cc_sec lo = (now / 300 + 1) * 300, hi = now + 365 * 86400;
  return t < lo ? lo : t > hi ? hi : t;
}

cc_sec cc_todo_morning(cc_sec now, uint16_t at_min) {
  int32_t day = cc_day_of(now);
  int m = cc_min_of(now);
  return cc_at(m < 300 && m < at_min ? day : day + 1, at_min);
}

bool cc_todos_read(CcTodos *l, const uint8_t *buf, int len) {
  memset(l, 0, sizeof(*l));
  if (len != (int)sizeof(*l)) return false;
  memcpy(l, buf, sizeof(*l));
  if (l->n > CC_TODO_MAX) { memset(l, 0, sizeof(*l)); return false; }
  for (int i = 0; i < CC_TODO_MAX; i++) l->t[i].name[CC_TODO_NAME - 1] = 0;
  return true;
}
