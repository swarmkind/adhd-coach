#include "t.h"
#include "cc_todo.h"
#include "cc_time.h"

#define D 20749  // Friday 2026-10-23

void test_todo(void) {
  T_TRUE(sizeof(CcTodos) <= 256);  // one persist value
  CcTodos l; memset(&l, 0, sizeof(l));
  cc_sec now = cc_at(D, 23 * 60 + 40);  // 23:40

  // The name is the start of the transcript, cut at a word, on one line.
  uint16_t a = cc_todo_add(&l, 111, "  Call the bank\nabout the blocked card before Friday", 0);
  T_TRUE(a != 0); T_EQ(l.n, 1);
  CcTodo *t = cc_todo_find(&l, a);
  T_STR(t->name, "Call the bank about the");
  T_EQ(t->note, 111); T_EQ(t->at, 0);
  uint16_t b = cc_todo_add(&l, 112, "Buy milk", now + 1800);
  T_TRUE(b != a);
  T_STR(cc_todo_find(&l, b)->name, "Buy milk");
  uint16_t c = cc_todo_add(&l, 113, "Pneumonoultramicroscopicsilicovolcanoconiosis", 0);  // one long word: cut anywhere
  T_EQ(strlen(cc_todo_find(&l, c)->name), CC_TODO_NAME - 1);
  uint16_t e = cc_todo_add(&l, 114, "Café crème à emporter près de chez moi", 0);  // never cut inside a character
  T_STR(cc_todo_find(&l, e)->name, "Café crème à emporter");
  CcTodos u; memset(&u, 0, sizeof(u));
  cc_todo_add(&u, 1, "ééééééééééééééééééééé", 0);  // one long word of two-byte letters
  T_EQ(strlen(u.t[0].name), 26);
  uint16_t f = cc_todo_add(&l, 115, "   ", 0);
  T_STR(cc_todo_find(&l, f)->name, "Voice note");
  T_EQ(cc_todo_add(&l, 112, "Buy milk and eggs", now + 600), b);  // the same note again: a new time, not a copy
  T_EQ(cc_todo_find(&l, b)->at, now + 600); T_STR(cc_todo_find(&l, b)->name, "Buy milk");
  T_TRUE(cc_todo_for_note(&l, 112) == cc_todo_find(&l, b));
  T_TRUE(cc_todo_for_note(&l, 999) == NULL);
  cc_todo_find(&l, b)->at = now + 1800;
  T_TRUE(cc_todo_add(&l, 116, "Six", 0) != 0);
  T_EQ(cc_todo_add(&l, 117, "Seven", 0), 0);  // full
  T_EQ(l.n, CC_TODO_MAX);
  T_EQ(cc_todo_add(&l, 112, "Buy milk", 0), b);  // a full list still takes a new time for one it has
  cc_todo_find(&l, b)->at = now + 1800;
  T_TRUE(cc_todo_find(&l, 999) == NULL);

  // Only the ones with a time fire, as normal-priority note fires, earliest first.
  cc_todo_find(&l, a)->at = now + 600;
  CcFire fs[8];
  int n = cc_todo_fires(&l, fs, 0, 8);
  T_EQ(n, 2);
  T_EQ(fs[0].at, now + 600); T_EQ(fs[0].id, a); T_EQ(fs[0].kind, CC_FIRE_NOTE); T_EQ(fs[0].priority, CC_NORMAL);
  T_EQ(fs[1].at, now + 1800); T_EQ(fs[1].id, b);

  // Answers from the alert.
  cc_todo_answer(&l, a, CC_A_LATER, now + 600, 30);
  T_EQ(cc_todo_find(&l, a)->at, now + 600 + 1800);
  cc_todo_answer(&l, a, CC_A_LATER_LONG, now + 700, 30);
  T_EQ(cc_todo_find(&l, a)->at, now + 700 + 3600);
  cc_todo_answer(&l, a, CC_A_IGNORED, now + 800, 30);  // ignored: one automatic Later
  T_EQ(cc_todo_find(&l, a)->at, now + 800 + 1800);
  cc_todo_answer(&l, a, CC_A_IGNORED, now + 900, 30);  // ignored again: stays in Today, quiet
  T_EQ(cc_todo_find(&l, a)->at, 0);
  cc_todo_remind(cc_todo_find(&l, a), now + 5000);    // a new time: one automatic Later again
  cc_todo_answer(&l, a, CC_A_IGNORED, now + 5000, 30);
  T_EQ(cc_todo_find(&l, a)->at, now + 5000 + 1800);
  cc_todo_answer(&l, b, CC_A_SKIP, now, 30);           // skip: no more reminding, still to do
  T_EQ(cc_todo_find(&l, b)->at, 0);
  cc_todo_answer(&l, a, CC_A_DONE, now, 30);           // done: gone
  T_TRUE(cc_todo_find(&l, a) == NULL); T_EQ(l.n, CC_TODO_MAX - 1);
  T_TRUE(cc_todo_find(&l, b) != NULL);
  cc_todo_answer(&l, 999, CC_A_DONE, now, 30);         // unknown: nothing
  T_EQ(l.n, CC_TODO_MAX - 1);
  T_TRUE(cc_todo_add(&l, 118, "Room again", 0) != 0);  // a freed place takes a new one

  // "In the morning": before 05:00 that is today's; from 05:00 on, tomorrow's.
  T_EQ(cc_todo_morning(now, 540), cc_at(D + 1, 540));
  T_EQ(cc_todo_morning(cc_at(D, 90), 540), cc_at(D, 540));
  T_EQ(cc_todo_morning(cc_at(D, 300), 540), cc_at(D + 1, 540));
  T_EQ(cc_todo_morning(cc_at(D, 600), 540), cc_at(D + 1, 540));

  // The picker: a day, an hour (wrapping in its day), 5 minutes (wrapping in its hour); never
  // before the next 5-minute mark from now, never more than a year ahead.
  cc_sec n0 = cc_at(D, 15 * 60 + 47);  // Friday 15:47
  cc_sec at = cc_at(D + 1, 540);        // Saturday 09:00
  T_EQ(cc_todo_pick(at, CC_PICK_DAY, 1, n0), cc_at(D + 2, 540));
  T_EQ(cc_todo_pick(at, CC_PICK_HOUR, 1, n0), cc_at(D + 1, 600));
  T_EQ(cc_todo_pick(at, CC_PICK_HOUR, -1, n0), cc_at(D + 1, 480));
  T_EQ(cc_todo_pick(cc_at(D + 1, 23 * 60), CC_PICK_HOUR, 1, n0), cc_at(D + 1, 0));  // the same day
  T_EQ(cc_todo_pick(at, CC_PICK_MIN, 1, n0), cc_at(D + 1, 545));
  T_EQ(cc_todo_pick(at, CC_PICK_MIN, -1, n0), cc_at(D + 1, 595));  // the same hour
  T_EQ(cc_todo_pick(cc_at(D + 1, 9 * 60 + 18), CC_PICK_MIN, 1, n0), cc_at(D + 1, 9 * 60 + 20));  // onto the 5s
  T_EQ(cc_todo_pick(at, CC_PICK_DAY, -1, n0), cc_at(D, 15 * 60 + 50));  // not in the past
  T_EQ(cc_todo_pick(cc_at(D, 16 * 60), CC_PICK_HOUR, -1, n0), cc_at(D, 15 * 60 + 50));
  T_EQ(cc_todo_pick(cc_at(D + 365, 540), CC_PICK_DAY, 1, n0), n0 + 365 * 86400);  // a year at most
  T_EQ(cc_todo_pick(at, CC_PICK_DAY, 0, n0), at);  // no step: just the limits

  // Saved and read back; anything else reads as none.
  CcTodos back;
  T_TRUE(cc_todos_read(&back, (const uint8_t *)&l, sizeof(l)));
  T_EQ(back.n, l.n); T_STR(back.t[0].name, l.t[0].name);
  T_TRUE(!cc_todos_read(&back, (const uint8_t *)&l, -1)); T_EQ(back.n, 0);
  T_TRUE(!cc_todos_read(&back, (const uint8_t *)&l, 10)); T_EQ(back.n, 0);
  l.n = CC_TODO_MAX + 1;
  T_TRUE(!cc_todos_read(&back, (const uint8_t *)&l, sizeof(l))); T_EQ(back.n, 0);
}
