#include <pebble.h>
#include "alert_win.h"
#include "poster.h"
#include "railtap.h"
#include "fittext.h"
#include "run_win.h"
#include "../platform/buzz.h"
#include "../platform/store.h"
#include "../platform/clock.h"
#include "../core/cc_time.h"
#include "../core/cc_sched.h"
#include "../core/cc_event.h"
#include "../core/cc_steps.h"

#define QUEUE_MAX 8
#define REBUZZ_MS 30000
#define IGNORE_MS 60000
#define PIP 13
#define PIP_GAP 4

static Window *s_win;
static Layer *s_canvas;
static FitText *s_title;
static CcFire s_fire;
static AlertDone s_done;
static const CcItem *s_item;
static AppTimer *s_rebuzz, *s_ignore;
static CcFire s_queue[QUEUE_MAX];
static AlertDone s_queue_done[QUEUE_MAX];
static int s_nqueue;
static char s_meta[40];
static bool s_menu_open;
static int s_menu_action;  // -1 = none chosen

static CcItem s_note;  // a note to-do, dressed as a task

static const CcItem *item_for(const CcFire *f) {
  if (f->kind == CC_FIRE_NOTE) {
    CcTodo *t = cc_todo_find(g_todos, f->id);
    if (!t) return NULL;
    memset(&s_note, 0, sizeof(s_note));
    s_note.kind = POSTER_VOICE;
    s_note.priority = CC_NORMAL;
    strcpy(s_note.name, t->name);
    return &s_note;
  }
  for (int i = 0; i < g_nitems; i++)
    if (g_items[i].id == f->id) return &g_items[i];
  return NULL;
}

static bool is_headsup(void) { return s_fire.kind == CC_FIRE_HEADSUP; }
static bool is_note(void) { return s_fire.kind == CC_FIRE_NOTE; }
static bool is_reminder(void) { return s_item->kind == CC_REMINDER; }
static bool is_event(void) { return s_item->kind == CC_EVENT; }
static bool has_steps(void) { return s_item->kind == CC_ROUTINE && s_item->step_count > 0; }

static void cancel_timers(void) {
  if (s_rebuzz) { app_timer_cancel(s_rebuzz); s_rebuzz = NULL; }
  if (s_ignore) { app_timer_cancel(s_ignore); s_ignore = NULL; }
}

static void buzz(void) {
  light_enable_interaction();
  if (is_headsup() || is_event()) buzz_headsup();
  else if (is_reminder()) buzz_nudge();
  else buzz_alert(s_item->priority);
}

static bool show_next(void);

// Report the action, then show the next queued alert in this window, or close it. The window
// is only removed when nothing is left, so a wakeup launch exits once the last alert is answered.
static void finish(CcAction a) {
  cancel_timers();
  CcFire fire = s_fire;
  AlertDone done = s_done;
  if (done) done(&fire, a);  // may open the runner on top of us
  if (!show_next()) window_stack_remove(s_win, true);
}

static void ignored(void *data) {
  s_ignore = NULL;
  // A heads-up never snoozes the item itself: its own alert follows within minutes.
  finish(is_headsup() ? CC_A_HEADSUP_SEEN : CC_A_IGNORED);
}

static void rebuzz(void *data) {
  s_rebuzz = NULL;
  buzz();
}

// Done with a number: saved or skipped (Back), the reminder counts as done either way.
static void number_done(bool saved, int32_t value) { finish(CC_A_DONE); }

static void on_select(ClickRecognizerRef r, void *ctx) {
  if (is_headsup()) {
    finish(CC_A_HEADSUP_SEEN);
  } else if (is_reminder() && (s_item->flags & CC_F_ASK_NUMBER)) {
    cancel_timers();
    number_win_open(s_item->id, CC_STEP_NONE, &s_item->num, number_done);
  } else {
    finish(has_steps() ? CC_A_START : CC_A_DONE);
  }
}
static void on_up(ClickRecognizerRef r, void *ctx) { if (!is_headsup()) finish(CC_A_LATER); }
static void on_down(ClickRecognizerRef r, void *ctx) { if (!is_headsup() && !is_event()) finish(CC_A_SKIP); }
// Back counts as Later, except on an event, where it just closes (spec 4.2): the rung is dealt with.
static void on_back(ClickRecognizerRef r, void *ctx) {
  finish(is_headsup() ? CC_A_HEADSUP_SEEN : is_event() ? CC_A_SKIP : CC_A_LATER);
}

static void menu_perform(ActionMenu *menu, const ActionMenuItem *action, void *ctx) {
  s_menu_action = (int)action_menu_item_get_action_data(action);
}

static void menu_closed(ActionMenu *menu, const ActionMenuItem *performed, void *ctx) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
  s_menu_open = false;
  if (s_menu_action >= 0) finish((CcAction)s_menu_action);
  else s_ignore = app_timer_register(IGNORE_MS, ignored, NULL);
}

static void on_select_long(ClickRecognizerRef r, void *ctx) {
  if (is_headsup() || s_menu_open) return;
  cancel_timers();
  ActionMenuLevel *level = action_menu_level_create(3);
  if (!is_event()) action_menu_level_add_action(level, "Done already", menu_perform, (void *)CC_A_DONE);
  action_menu_level_add_action(level, is_note() ? "Stop reminding" : "Pause until I resume", menu_perform,
                               (void *)CC_A_PAUSE);
  action_menu_level_add_action(level, "Later 60 min", menu_perform, (void *)CC_A_LATER_LONG);
  s_menu_open = true;
  s_menu_action = -1;
  poster_open_menu(level, poster_kind_color(s_item->kind), menu_closed);
}

static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, on_select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, on_select_long, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, on_up);
  window_single_click_subscribe(BUTTON_ID_DOWN, on_down);
  window_single_click_subscribe(BUTTON_ID_BACK, on_back);
}

static void fmt_every(uint16_t every, char *buf, int len) {
  if (every == 60) snprintf(buf, len, "every hour");
  else if (every % 60 == 0) snprintf(buf, len, "every %d h", every / 60);
  else snprintf(buf, len, "every %d min", every);
}

// The small line above the title: "10:00 · every hour", "07:00 · 4 steps" ("1 step"), "in 10 min · 14:00".
static void fmt_meta(void) {
  char t[12], e[16];
  CcItemState *st = cc_state_for(g_day, s_item->id);
  if (is_note()) {
    poster_fmt_time(cc_min_of(s_fire.at), t, sizeof(t));
    snprintf(s_meta, sizeof(s_meta), "note · %s", t);
  } else if (is_headsup()) {
    poster_fmt_time(s_item->at_min, t, sizeof(t));
    int lead = s_item->priority == CC_CRITICAL ? g_cfg.headsup_critical_min : g_cfg.headsup_important_min;
    int mins = (int)((s_fire.at + lead * 60 - clock_now_local() + 59) / 60);  // the heads-up is due - lead
    if (mins < 1) mins = 1;
    snprintf(s_meta, sizeof(s_meta), "in %d min · %s", mins, t);
  } else if (is_event()) {  // "in 3 days · Thu 10:30", "tomorrow · 10:30", "in 45 min · 10:30"
    cc_sec now = clock_now_local();
    poster_fmt_time(s_item->at_min, t, sizeof(t));
    int days = (int)(s_item->event_day - cc_day_of(now));
    int mins = (int)((cc_event_at(s_item) - now + 59) / 60);
    if (mins < 90) snprintf(s_meta, sizeof(s_meta), "in %d min · %s", mins < 1 ? 1 : mins, t);
    else if (days <= 0) snprintf(s_meta, sizeof(s_meta), "today · %s", t);
    else if (days == 1) snprintf(s_meta, sizeof(s_meta), "tomorrow · %s", t);
    else snprintf(s_meta, sizeof(s_meta), "in %d days · %s %s", days, poster_wday(s_item->event_day), t);
  } else if (is_reminder()) {
    poster_fmt_time(cc_min_of(cc_occurrence_for(s_item, st, s_fire.at, &g_cfg)), t, sizeof(t));
    if (s_item->until_min > s_item->at_min) {
      fmt_every(s_item->every_min, e, sizeof(e));
      snprintf(s_meta, sizeof(s_meta), "%s · %s", t, e);
    } else {
      snprintf(s_meta, sizeof(s_meta), "%s", t);
    }
  } else {
    poster_fmt_time(s_item->at_min, t, sizeof(t));
    uint8_t order[CC_MAX_STEPS];
    int n = has_steps() ? cc_steps_today(s_item, cc_item_steps(s_item), cc_day_of(s_fire.at), order) : 0;
    if (n) snprintf(s_meta, sizeof(s_meta), "%s · %d step%s", t, n, n == 1 ? "" : "s");
    else snprintf(s_meta, sizeof(s_meta), "%s", t);
  }
}

static void draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor bg = poster_kind_color(s_item->kind);
  poster_draw_background(ctx, b, bg);
  RailAction rail[3] = { { G_LATER, "later" }, { G_DONE, "done" }, { G_SKIP, "skip" } };
  if (is_headsup()) {
    rail[0] = (RailAction){ G_NONE, NULL };
    rail[1] = (RailAction){ G_DONE, "got it" };
    rail[2] = (RailAction){ G_NONE, NULL };
  } else if (is_event()) {
    rail[1] = (RailAction){ G_DONE, "got it" };
    rail[2] = (RailAction){ G_NONE, NULL };
  } else if (has_steps()) {
    rail[1] = (RailAction){ G_START, "start" };
  }
  poster_draw_rail(ctx, b, bg, rail);

  graphics_context_set_text_color(ctx, GColorBlack);
  poster_text(ctx, s_meta, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(POSTER_TEXT_X, 6, POSTER_TEXT_W, 24));

  if (is_reminder() && !is_headsup() && s_item->goal) {
    CcItemState *st = cc_state_for(g_day, s_item->id);
    int count = st ? st->count : 0;
    if (s_item->goal <= 8) {  // pips fit up to 8 across the text box
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_context_set_stroke_width(ctx, 1);
      for (int i = 0; i < s_item->goal; i++) {
        GRect pip = GRect(POSTER_TEXT_X + i * (PIP + PIP_GAP), 150, PIP, PIP);
        if (i < count) graphics_fill_rect(ctx, pip, 0, GCornerNone);
        else {
          graphics_draw_rect(ctx, pip);
          graphics_draw_rect(ctx, grect_inset(pip, GEdgeInsets(1)));
        }
      }
    }
    char line[24];
    snprintf(line, sizeof(line), "%d of %d today", count, s_item->goal);
    poster_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(POSTER_TEXT_X, 168, POSTER_TEXT_W, 24));
  }
}

static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, draw);
  layer_add_child(root, s_canvas);
  s_title = fittext_create(GRect(POSTER_TEXT_X, 30, POSTER_TEXT_W, 72), fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                           fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 2, GColorBlack);
  layer_add_child(root, fittext_layer(s_title));
  fittext_set(s_title, s_item->name);
}

static void unload(Window *w) {
  fittext_destroy(s_title);
  s_title = NULL;
  layer_destroy(s_canvas);
}

static bool show_next(void) {
  while (s_nqueue > 0) {
    s_fire = s_queue[0];
    s_done = s_queue_done[0];
    memmove(s_queue, s_queue + 1, sizeof(CcFire) * (s_nqueue - 1));
    memmove(s_queue_done, s_queue_done + 1, sizeof(AlertDone) * (s_nqueue - 1));
    s_nqueue--;
    s_item = item_for(&s_fire);
    if (!s_item) continue;  // deleted since it was planned, or a step end (the run window owns those)
    if (!s_win) {
      s_win = window_create();
      window_set_window_handlers(s_win, (WindowHandlers){ .load = load, .unload = unload });
      window_set_click_config_provider(s_win, click_config);
      railtap_register(s_win, &(RailTaps){ on_up, on_select, on_down, on_select_long, NULL }, false);
    }
    fmt_meta();
    if (!window_stack_contains_window(s_win)) {
      window_stack_push(s_win, true);
    } else if (window_stack_get_top_window() != s_win) {  // something opened on top: come back up
      window_stack_remove(s_win, false);
      window_stack_push(s_win, true);
    } else {  // already in front: swap the content in place
      fittext_set(s_title, s_item->name);
      layer_mark_dirty(s_canvas);
    }
    buzz();
    s_rebuzz = app_timer_register(REBUZZ_MS, rebuzz, NULL);
    s_ignore = app_timer_register(IGNORE_MS, ignored, NULL);
    return true;
  }
  return false;
}

void alert_win_push(const CcFire *fire, AlertDone done) {
  bool showing = s_win && window_stack_contains_window(s_win);
  if (showing && s_fire.id == fire->id && s_fire.kind == fire->kind) return;
  for (int i = 0; i < s_nqueue; i++)
    if (s_queue[i].id == fire->id && s_queue[i].kind == fire->kind) return;
  if (s_nqueue >= QUEUE_MAX) return;
  s_queue[s_nqueue] = *fire;
  s_queue_done[s_nqueue] = done;
  s_nqueue++;
  if (!showing) show_next();
}
