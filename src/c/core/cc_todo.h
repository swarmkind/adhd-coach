#pragma once
// Note to-dos: a voice note turned into something to do, kept on the watch only. Each shows in
// Today; one with a time alerts then, like a task.
#include "cc_types.h"
#include "cc_state.h"
#include "cc_fires.h"

#define CC_TODO_MAX 6
#define CC_TODO_NAME 28  // bytes incl. NUL: six to-dos fit one 256-byte persist value

typedef struct {
  uint16_t id;
  uint8_t later_used;  // the one automatic Later of an ignored alert
  cc_sec at;           // the reminder; 0 = none
  uint32_t note;       // the note it came from
  char name[CC_TODO_NAME];
} CcTodo;
typedef struct { uint8_t n; uint16_t next_id; CcTodo t[CC_TODO_MAX]; } CcTodos;

// Its id; 0 when full. A note already on the list gets the new time instead of a second to-do.
uint16_t cc_todo_add(CcTodos *l, uint32_t note, const char *text, cc_sec at);
CcTodo *cc_todo_for_note(CcTodos *l, uint32_t note);
CcTodo *cc_todo_find(CcTodos *l, uint16_t id);
void cc_todo_remind(CcTodo *t, cc_sec at);
void cc_todo_answer(CcTodos *l, uint16_t id, CcAction a, cc_sec now, int later_min);  // from its alert
int cc_todo_fires(const CcTodos *l, CcFire f[], int n, int max);
// The reminder picker: one step of the day, the hour (wrapping in its day) or the minutes (5 at a time,
// wrapping in the hour), kept from the next 5-minute mark after now to a year ahead.
enum { CC_PICK_DAY, CC_PICK_HOUR, CC_PICK_MIN };
cc_sec cc_todo_pick(cc_sec at, int field, int dir, cc_sec now);
cc_sec cc_todo_morning(cc_sec now, uint16_t at_min);  // the coming morning's at_min: today's before 05:00
bool cc_todos_read(CcTodos *l, const uint8_t *buf, int len);
