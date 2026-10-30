#include <pebble.h>
#include "today_win.h"
#include "poster.h"
#include "run_win.h"
#include "settings_win.h"
#include "focus_win.h"
#include "voice_win.h"
#include "home_win.h"
#include "when_win.h"
#include "../platform/notes.h"
#include "../coach.h"
#include "../platform/store.h"
#include "../platform/clock.h"
#include "../platform/wake.h"
#include "../core/cc_time.h"
#include "../core/cc_event.h"
#include "../core/cc_sched.h"
#include "../core/cc_steps.h"

#define ROW_H 52
#define TEXT_X 12
#define TICK_MS 40

static Window *s_win;
static MenuLayer *s_menu;
static uint8_t s_rows[CC_MAX_ITEMS];  // slots, in display order
static int s_nrows;
static uint16_t s_menu_item;      // item the open ActionMenu acts on
static uint16_t s_number_item;    // reminder waiting on number entry
static uint16_t s_focus_item;     // task chosen for "Focus on this"
static int s_nnotes;              // for the voice log row
static int s_ntodos;              // note to-dos, after the items
static uint16_t s_menu_todo;      // to-do the open ActionMenu acts on
static uint32_t s_open_note;      // note chosen for "Open note"
static cc_sec s_todo_now;         // the to-do menu's times are from when it opened
static bool s_pick;               // "Pick day and time" chosen: the picker opens once the menu has closed

// Marquee for the highlighted row (same timing as FitText).
static AppTimer *s_marq_timer;
static int s_marq_ms, s_marq_loops, s_marq_offset, s_marq_w;
static bool s_marq_on;

enum { A_START, A_DONE, A_SKIP, A_LATER, A_RESUME, A_FOCUS };
enum { T_DONE, T_AT, T_NONE = T_AT + VOICE_TODO_TIMES, T_OPEN, T_PICK };

// An event shows on its own day and on each day it has a rung.
static bool today_has(const CcItem *it, int32_t day) {
  if (it->kind == CC_EVENT) return it->event_day == day || cc_event_first_rung_on(it, day, &g_cfg);
  return cc_item_on_day(it, day);
}

// Timed items by time (an event by its rung, or its own time on the day), anytime tasks
// after them by `order`.
static int row_key(const CcItem *it, int32_t day) {
  if (it->flags & CC_F_ANYTIME) return 10000 + it->order;
  if (it->kind == CC_EVENT && it->event_day != day) return cc_min_of(cc_event_first_rung_on(it, day, &g_cfg));
  return it->at_min;
}

static void build_rows(void) {
  int32_t day = cc_day_of(clock_now_local());
  s_nrows = 0;
  s_nnotes = notes_count();
  s_ntodos = g_todos->n;
  for (int i = 0; i < g_nitems; i++) {
    const CcItem *it = &g_items[i];
    if (!today_has(it, day)) continue;
    int key = row_key(it, day);
    int j = s_nrows++;
    while (j > 0) {
      if (row_key(&g_items[s_rows[j - 1]], day) <= key) break;
      s_rows[j] = s_rows[j - 1];
      j--;
    }
    s_rows[j] = i;
  }
}

static void sub_line(const CcItem *it, char *buf, int len) {
  char t[12], u[12];
  CcItemState *st = cc_state_for(g_day, it->id);
  int status = st ? st->status : CC_ST_NONE;
  if (it->flags & CC_F_ANYTIME) snprintf(t, sizeof(t), "anytime");
  else poster_fmt_time(it->at_min, t, sizeof(t));
  if (it->kind == CC_EVENT) {  // "Sat 31 Oct · 10:30 · in 8 days", "10:30 · today"
    int32_t today = cc_day_of(clock_now_local());
    int days = (int)(it->event_day - today);
    cc_sec last = cc_slot_at_or_before(it, cc_at(today, 1439) + 59, &g_cfg);  // today's last rung
    char state[24] = "", d[16];
    if (status == CC_ST_PAUSED) snprintf(state, sizeof(state), "off today");
    else if (st && st->later_until) {
      poster_fmt_time(cc_min_of(st->later_until), u, sizeof(u));
      snprintf(state, sizeof(state), "later %s", u);
    } else if (last && st && st->handled_until >= last) {  // every rung today dealt with
      snprintf(state, sizeof(state), "%s", !st->count && st->missed ? "missed" : "got it");
    } else if (days == 0) snprintf(state, sizeof(state), "today");
    else if (days == 1) snprintf(state, sizeof(state), "tomorrow");
    else snprintf(state, sizeof(state), "in %d days", days);
    if (days == 0) { snprintf(buf, len, "%s · %s", t, state); return; }
    poster_fmt_date(it->event_day, d, sizeof(d));
    snprintf(buf, len, "%s · %s · %s", d, t, state);
    return;
  }
  if (it->kind == CC_REMINDER) {
    poster_fmt_time(it->until_min, u, sizeof(u));
    int count = st ? st->count : 0;
    if (status == CC_ST_PAUSED) snprintf(buf, len, "%s-%s · off today", t, u);
    else if (it->goal) snprintf(buf, len, "%s-%s · %d of %d", t, u, count, it->goal);
    else snprintf(buf, len, "%s-%s · %d done", t, u, count);
    return;
  }
  static const char *words[] = { "", "started", "done", "skipped", "missed", "paused" };
  if (status == CC_ST_NONE && st && st->later_until) {
    poster_fmt_time(cc_min_of(st->later_until), u, sizeof(u));
    snprintf(buf, len, "%s · later %s", t, u);
  } else if (status == CC_ST_NONE) {
    snprintf(buf, len, "%s", t);
  } else {
    snprintf(buf, len, "%s · %s", t, words[status]);
  }
}

// "note", "note · 15:30", "note · tomorrow 09:00", "note · Sat 09:00", "note · Sat 14 Nov 09:00"
static void todo_sub(const CcTodo *t, char *buf, int len) {
  if (!t->at) { snprintf(buf, len, "note"); return; }
  char u[12];
  poster_fmt_time(cc_min_of(t->at), u, sizeof(u));
  int days = (int)(cc_day_of(t->at) - cc_day_of(clock_now_local()));
  if (days <= 0) snprintf(buf, len, "note · %s", u);
  else if (days == 1) snprintf(buf, len, "note · tomorrow %s", u);
  else if (days < 7) snprintf(buf, len, "note · %s %s", poster_wday(cc_day_of(t->at)), u);
  else {
    char d[16];
    poster_fmt_date(cc_day_of(t->at), d, sizeof(d));
    snprintf(buf, len, "note · %s %s", d, u);
  }
}

static bool is_todo_row(int row) { return row >= s_nrows && row < s_nrows + s_ntodos; }

static const char *row_name(int row) {
  if (row < s_nrows) return g_items[s_rows[row]].name;
  if (is_todo_row(row)) return g_todos->t[row - s_nrows].name;
  return NULL;
}

static void marq_stop(void) {
  if (s_marq_timer) { app_timer_cancel(s_marq_timer); s_marq_timer = NULL; }
}

static void marq_tick(void *data) {
  s_marq_timer = NULL;
  int end = s_marq_w + 4 - (layer_get_bounds(menu_layer_get_layer(s_menu)).size.w - 2 * TEXT_X);
  s_marq_ms += TICK_MS;
  if (s_marq_ms <= 1000) s_marq_offset = 0;
  else s_marq_offset = (s_marq_ms - 1000) * 30 / 1000;
  if (s_marq_offset >= end) {
    s_marq_offset = end;
    if (s_marq_ms >= 1000 + end * 1000 / 30 + 800) {  // end pause over: next loop
      s_marq_ms = 0;
      s_marq_offset = 0;
      if (++s_marq_loops >= 3) s_marq_on = false;
    }
  }
  layer_mark_dirty(menu_layer_get_layer(s_menu));
  if (s_marq_on) s_marq_timer = app_timer_register(TICK_MS, marq_tick, NULL);
}

static void marq_restart(void) {
  marq_stop();
  s_marq_on = false;
  s_marq_ms = s_marq_loops = s_marq_offset = 0;
  const char *name = row_name(menu_layer_get_selected_index(s_menu).row);
  if (!name) return;
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_marq_w = graphics_text_layout_get_content_size(name, f, GRect(0, 0, 2000, 40), GTextOverflowModeWordWrap,
                                                   GTextAlignmentLeft).w;
  int w = layer_get_bounds(menu_layer_get_layer(s_menu)).size.w - 2 * TEXT_X;
  if (s_marq_w > w) {
    s_marq_on = true;
    s_marq_timer = app_timer_register(TICK_MS, marq_tick, NULL);
  }
}

// The item rows, the note to-dos (or "Nothing today"), then the voice log, then Settings.
static int voice_row(void) { return s_nrows + s_ntodos ? s_nrows + s_ntodos : 1; }
static int settings_row(void) { return voice_row() + 1; }
static uint16_t num_rows(MenuLayer *m, uint16_t section, void *ctx) { return (uint16_t)(settings_row() + 1); }
static int16_t row_h(MenuLayer *m, MenuIndex *i, void *ctx) { return ROW_H; }

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
  char sub[64];
  if (idx->row == voice_row()) {
    if (s_nnotes) snprintf(sub, sizeof(sub), s_nnotes == 1 ? "1 note" : "%d notes", s_nnotes);
    else snprintf(sub, sizeof(sub), "none yet");
    poster_draw_row(ctx, cell, poster_kind_color(POSTER_VOICE), true, "Voice notes", sub);
    return;
  }
  if (idx->row == settings_row()) {
    poster_draw_row(ctx, cell, poster_kind_color(POSTER_NOTHING), false, "Settings", NULL);
    return;
  }
  GFont name_f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  if (!s_nrows && !s_ntodos) {
    graphics_context_set_text_color(ctx, GColorWhite);
    poster_text(ctx, "Nothing today", name_f, GRect(TEXT_X, 8, layer_get_bounds(cell).size.w - 2 * TEXT_X, 30));
    return;
  }
  const char *name = row_name(idx->row);
  GColor color;
  if (is_todo_row(idx->row)) {
    todo_sub(&g_todos->t[idx->row - s_nrows], sub, sizeof(sub));
    color = poster_kind_color(POSTER_VOICE);
  } else {
    const CcItem *it = &g_items[s_rows[idx->row]];
    sub_line(it, sub, sizeof(sub));
    color = poster_kind_color(it->kind);
  }
  bool marq = s_marq_on && menu_cell_layer_is_highlighted(cell);  // the selected name scrolls
  poster_draw_row(ctx, cell, color, true, marq ? NULL : name, sub);  // a strip of the kind colour marks each row
  if (marq) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, name, name_f, GRect(TEXT_X - s_marq_offset, 0, s_marq_w + 12, 30),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }
}

static void number_done(bool saved, int32_t value) {
  coach_apply(s_number_item, CC_A_DONE, coach_current_occurrence(s_number_item));
}

static void menu_perform(ActionMenu *menu, const ActionMenuItem *action, void *ctx) {
  const CcItem *it = coach_item(s_menu_item);
  if (!it) return;
  bool rem = it->kind == CC_REMINDER, slots = rem || it->kind == CC_EVENT;
  cc_sec occ = slots ? coach_current_occurrence(it->id) : 0;
  switch ((int)action_menu_item_get_action_data(action)) {
    case A_RESUME: coach_apply(it->id, CC_A_RESUME, 0); break;
    case A_START: coach_start(it->id); break;
    case A_DONE:
      if (rem && (it->flags & CC_F_ASK_NUMBER)) {
        s_number_item = it->id;
        number_win_open(it->id, CC_STEP_NONE, &it->num, number_done);
      } else {
        coach_apply(it->id, CC_A_DONE, occ);
      }
      break;
    case A_SKIP: coach_apply(it->id, slots ? CC_A_PAUSE : CC_A_SKIP, occ); break;  // reminders and events: off for the day
    case A_LATER: coach_apply(it->id, CC_A_LATER, occ); break;
    case A_FOCUS: s_focus_item = it->id; break;  // opened once the menu has closed
  }
}

static void todo_saved(void) {
  store_save_todos();
  wake_rearm();
  ui_refresh_all();
}

static void todo_picked(cc_sec at) {
  CcTodo *t = cc_todo_find(g_todos, s_menu_todo);
  if (!t) return;
  cc_todo_remind(t, at);
  todo_saved();
}

static void todo_perform(ActionMenu *menu, const ActionMenuItem *action, void *ctx) {
  CcTodo *t = cc_todo_find(g_todos, s_menu_todo);
  if (!t) return;
  int a = (int)action_menu_item_get_action_data(action);
  if (a == T_OPEN) { s_open_note = t->note; return; }  // opened once the menu has closed
  if (a == T_PICK) { s_pick = true; return; }
  if (a == T_DONE) cc_todo_answer(g_todos, t->id, CC_A_DONE, 0, 0);
  else cc_todo_remind(t, a == T_NONE ? 0 : voice_todo_at(a - T_AT, s_todo_now));
  todo_saved();
}

static void menu_closed(ActionMenu *menu, const ActionMenuItem *performed, void *ctx) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
  if (s_focus_item) focus_win_open(s_focus_item);
  if (s_open_note) voice_note_open(s_open_note);
  const CcTodo *t = s_pick ? cc_todo_find(g_todos, s_menu_todo) : NULL;
  if (t) when_win_open(voice_todo_pick_start(t->at, s_todo_now), todo_picked);
  s_focus_item = 0;
  s_open_note = 0;
  s_pick = false;
}

static bool note_exists(uint32_t id) {
  CcNote n;
  char *text = malloc(CC_NOTE_TEXT_MAX + 1);
  bool ok = text && notes_read(id, &n, text);
  free(text);
  return ok;
}

static void todo_menu(const CcTodo *t) {
  s_menu_todo = t->id;
  s_todo_now = clock_now_local();
  ActionMenuLevel *level = action_menu_level_create(7);
  action_menu_level_add_action(level, "Done", todo_perform, (void *)T_DONE);
  for (int i = 0; i < VOICE_TODO_TIMES; i++)
    action_menu_level_add_action(level, voice_todo_label(i, s_todo_now), todo_perform, (void *)(T_AT + i));
  action_menu_level_add_action(level, "Pick day and time", todo_perform, (void *)T_PICK);
  if (t->at) action_menu_level_add_action(level, "No reminder", todo_perform, (void *)T_NONE);
  if (note_exists(t->note)) action_menu_level_add_action(level, "Open note", todo_perform, (void *)T_OPEN);
  poster_open_menu(level, poster_kind_color(POSTER_VOICE), menu_closed);
}

static void select_click(MenuLayer *m, MenuIndex *idx, void *ctx) {
  if (idx->row == settings_row()) { settings_win_open(0); return; }
  if (idx->row == voice_row()) { voice_log_open(); return; }
  if (is_todo_row(idx->row)) { todo_menu(&g_todos->t[idx->row - s_nrows]); return; }
  if (idx->row >= s_nrows) return;
  const CcItem *it = &g_items[s_rows[idx->row]];
  s_menu_item = it->id;
  const CcItemState *st = cc_state_for(g_day, it->id);
  ActionMenuLevel *level = action_menu_level_create(6);
  if (st && st->status == CC_ST_PAUSED) action_menu_level_add_action(level, "Resume", menu_perform, (void *)A_RESUME);
  bool ev = it->kind == CC_EVENT;
  if (it->kind != CC_REMINDER && !ev) action_menu_level_add_action(level, "Start", menu_perform, (void *)A_START);
  action_menu_level_add_action(level, ev ? "Got it" : "Done", menu_perform, (void *)A_DONE);
  action_menu_level_add_action(level, "Skip today", menu_perform, (void *)A_SKIP);
  action_menu_level_add_action(level, "Later", menu_perform, (void *)A_LATER);
  if (it->kind == CC_TASK) action_menu_level_add_action(level, "Focus on this", menu_perform, (void *)A_FOCUS);
  poster_open_menu(level, poster_kind_color(it->kind), menu_closed);
}

static void selection_changed(MenuLayer *m, MenuIndex new_idx, MenuIndex old_idx, void *ctx) { marq_restart(); }

static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  window_set_background_color(w, GColorBlack);
  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_normal_colors(s_menu, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_menu, GColorBlack, GColorBlack);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = num_rows,
    .get_cell_height = row_h,
    .draw_row = draw_row,
    .select_click = select_click,
    .selection_changed = selection_changed,
  });
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void appear(Window *w) {
  build_rows();
  menu_layer_reload_data(s_menu);
  marq_restart();
}

static void disappear(Window *w) { marq_stop(); }

static void unload(Window *w) {
  marq_stop();
  menu_layer_destroy(s_menu);
  s_menu = NULL;
}

void today_win_open(void) {
  if (!s_win) {
    s_win = window_create();
    window_set_window_handlers(s_win, (WindowHandlers){ .load = load, .appear = appear, .disappear = disappear, .unload = unload });
  }
  window_stack_push(s_win, true);
}

void today_win_refresh(void) {
  if (!s_win || !window_stack_contains_window(s_win) || !s_menu) return;
  build_rows();
  menu_layer_reload_data(s_menu);
}

bool today_win_on_top(void) { return s_win && window_stack_get_top_window() == s_win; }
