#include <pebble.h>
#include "home_win.h"
#include "today_win.h"
#include "alarms_win.h"
#include "poster.h"
#include "fittext.h"
#include "run_win.h"
#include "focus_win.h"
#include "railtap.h"
#include "../platform/voice.h"
#include "../coach.h"
#include "../core/cc_battery.h"
#include "../platform/store.h"
#include "../platform/clock.h"
#include "when_win.h"
#include "../config.h"
#include "../core/cc_fires.h"
#include "../core/cc_time.h"
#include "../core/cc_steps.h"

#define BLOCK_Y 82
#define BLOCK_H 86
#define PIP 9
#define PIP_GAP 3

typedef enum { H_NEXT, H_PARKED, H_FOCUS, H_ALL_DONE, H_NOTHING } HomeState;

static Window *s_win;
static Layer *s_canvas;
static FitText *s_name;
static HomeState s_state;
static GColor s_color;
static char s_when[32];
static char s_later[64];          // "tomorrow 07:00 · Morning routine" under "All done for today"
static int s_goal_slot = -1;      // first reminder with a goal

static void fmt_when(cc_sec at, cc_sec now, char *buf, int len) {
  char t[12];
  poster_fmt_time(cc_min_of(at), t, sizeof(t));
  int32_t days = cc_day_of(at) - cc_day_of(now);
  int mins = (int)((at - now + 59) / 60);
  if (at <= now) snprintf(buf, len, "now");
  else if (mins < 60) snprintf(buf, len, "in %d min", mins);
  else if (days == 0) snprintf(buf, len, "at %s", t);
  else if (days == 1) snprintf(buf, len, "tomorrow %s", t);
  else {
    time_t u = at - clock_tz_offset();
    char wd[6];
    strftime(wd, sizeof(wd), "%a", localtime(&u));
    snprintf(buf, len, "%s %s", wd, t);
  }
}

static void compute(void) {
  cc_sec now = clock_now_local();
  s_goal_slot = -1;
  for (int i = 0; i < g_nitems && s_goal_slot < 0; i++)
    if (g_items[i].kind == CC_REMINDER && g_items[i].goal && cc_item_on_day(&g_items[i], cc_day_of(now)))
      s_goal_slot = i;

  if (g_focus.active) {  // a running Focus outranks a parked routine
    s_state = H_FOCUS;
    s_color = poster_kind_color(POSTER_FOCUS);
    const char *what = g_focus.phase == CC_FOCUS_WORK ? "focus" : "break";
    if (g_focus.paused) snprintf(s_when, sizeof(s_when), "%s · paused", what);
    else snprintf(s_when, sizeof(s_when), "%s · %d min left", what, (int)((cc_focus_remaining(&g_focus, now) + 59) / 60));
    const CcItem *lab = coach_item(g_focus.label_id);
    fittext_set(s_name, lab ? lab->name : "Focus");
    return;
  }
  const CcItem *run = g_run.active ? coach_item(g_run.item_id) : NULL;
  if (run) {
    s_state = H_PARKED;
    s_color = poster_kind_color(run->kind);
    snprintf(s_when, sizeof(s_when), "paused · %d of %d", g_run.step + 1, g_run.step_count);
    fittext_set(s_name, run->name);
    return;
  }
  CcFire f[16];
  int n = cc_collect(g_items, g_nitems, g_day, CC_NEVER, CC_NEVER, now, &g_cfg, f, 16);
  const CcItem *next = NULL;
  cc_sec at = 0;
  for (int i = 0; i < n && !next; i++)
    if (f[i].kind == CC_FIRE_DUE) { next = coach_item(f[i].id); at = f[i].at; }
  if (!next) {
    s_state = H_NOTHING;
    s_color = poster_kind_color(255);
  } else if (cc_day_of(at) != cc_day_of(now)) {
    s_state = H_ALL_DONE;
    s_color = poster_kind_color(255);
    char w[24];
    fmt_when(at, now, w, sizeof(w));
    snprintf(s_later, sizeof(s_later), "%s · %s", w, next->name);
  } else {
    s_state = H_NEXT;
    s_color = poster_kind_color(next->kind);
    fmt_when(at, now, s_when, sizeof(s_when));
    fittext_set(s_name, next->name);
  }
}

static void draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  cc_sec now = clock_now_local();
  poster_draw_background(ctx, b, s_color);

  char t[12], d[20];
  poster_fmt_clock(cc_min_of(now), t, sizeof(t));
  time_t u = time(NULL);
  struct tm *lt = localtime(&u);
  char wd[6], mo[6];
  strftime(wd, sizeof(wd), "%a", lt);
  strftime(mo, sizeof(mo), "%b", lt);
  snprintf(d, sizeof(d), "%s %d %s", wd, lt->tm_mday, mo);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, t, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS), GRect(POSTER_TEXT_X, 8, POSTER_TEXT_W, 48),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  poster_text(ctx, d, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(POSTER_TEXT_X, 54, POSTER_TEXT_W, 24));
  cc_sec ring = CC_NEVER;  // the next alarm within a day: a bell and its time, at the right of the date
  for (int i = 0; i < g_alarms.n; i++) {
    cc_sec at = cc_alarm_next(&g_alarms.a[i], now);
    if (at < ring) ring = at;
  }
  if (ring - now < CC_DAY) {
    GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    poster_fmt_clock(cc_min_of(ring), t, sizeof(t));
    int aw = graphics_text_layout_get_content_size(t, f, GRect(0, 0, POSTER_TEXT_W, 24), GTextOverflowModeFill,
                                                   GTextAlignmentLeft).w;
    int ax = POSTER_TEXT_X + POSTER_TEXT_W - aw;
    graphics_draw_text(ctx, t, f, GRect(ax, 54, aw + 2, 24), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    poster_draw_glyph(ctx, G_ALARM, GPoint(ax - 11, 66), GColorBlack);
  }

  int w = b.size.w - POSTER_RAIL_W;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, BLOCK_Y, w, BLOCK_H), 0, GCornerNone);
  if (s_state == H_NEXT || s_state == H_PARKED || s_state == H_FOCUS) {
    graphics_context_set_text_color(ctx, s_color);
    poster_text(ctx, s_when, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                GRect(POSTER_TEXT_X, BLOCK_Y + 6, POSTER_TEXT_W, 22));
  } else {
    graphics_context_set_text_color(ctx, GColorWhite);
    const char *msg = s_state == H_NOTHING ? "Nothing scheduled" : "All done for today";
    graphics_draw_text(ctx, msg, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(POSTER_TEXT_X, BLOCK_Y + 12, POSTER_TEXT_W, 60),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    if (s_state == H_ALL_DONE) {
      graphics_context_set_text_color(ctx, GColorLightGray);
      poster_text(ctx, s_later, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                  GRect(POSTER_TEXT_X, BLOCK_Y + 58, POSTER_TEXT_W, 22));
    }
  }

  if (s_goal_slot >= 0) {  // reminder progress: pips and "4/8"
    const CcItem *it = &g_items[s_goal_slot];
    CcItemState *st = cc_state_for(g_day, it->id);
    int count = st ? st->count : 0, x = POSTER_TEXT_X, y = BLOCK_Y + BLOCK_H + 20;
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 1);
    if (it->goal <= 8) {
      for (int i = 0; i < it->goal; i++, x += PIP + PIP_GAP) {
        GRect pip = GRect(x, y, PIP, PIP);
        if (i < count) graphics_fill_rect(ctx, pip, 0, GCornerNone);
        else graphics_draw_rect(ctx, pip);
      }
      x += 3;
    }
    char c[12];
    snprintf(c, sizeof(c), "%d/%d", count, it->goal);
    graphics_context_set_text_color(ctx, GColorBlack);
    poster_text(ctx, c, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(x, y - 7, 60, 24));
    poster_text(ctx, it->name, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                GRect(POSTER_TEXT_X, y + 12, POSTER_TEXT_W, 18));
  }

  RailAction rail[3] = { { G_FOCUS, "focus" }, { G_TODAY, "today" }, { G_NOTE, "note" } };
  if (s_state == H_PARKED || s_state == H_FOCUS) rail[1] = (RailAction){ G_RESUME, "go" };
  poster_draw_rail(ctx, b, s_color, rail);

  // Battery, above "focus": a battery with five bars inside, the last one left red at 20% or less.
  // The minute redraw keeps it current.
  uint8_t pct = battery_state_service_peek().charge_percent;
  int bars = cc_battery_bars(pct), x = b.size.w - POSTER_RAIL_W + 4;
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_rect(ctx, GRect(x, 10, 28, 12));
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(x + 28, 13, 2, 6), 0, GCornerNone);  // the terminal
  graphics_context_set_fill_color(ctx, cc_battery_low(pct) ? GColorRed : s_color);
  for (int i = 0; i < bars; i++) graphics_fill_rect(ctx, GRect(x + 2 + i * 5, 12, 4, 8), 0, GCornerNone);
}

static void on_select(ClickRecognizerRef r, void *ctx) {
  if (s_state == H_FOCUS) focus_win_open(0);
  else if (s_state == H_PARKED) run_win_open();
  else today_win_open();
}

static void on_up(ClickRecognizerRef r, void *ctx) { focus_win_open(0); }

enum { M_TODAY, M_ALARMS, M_EXIT };
static bool s_menu_open;
static int s_menu_choice;  // -1 = none

static void exit_app(void *data) { window_stack_pop_all(false); }
static void menu_perform(ActionMenu *menu, const ActionMenuItem *action, void *ctx) {
  s_menu_choice = (int)action_menu_item_get_action_data(action);
}

static void menu_closed(ActionMenu *menu, const ActionMenuItem *performed, void *ctx) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
  s_menu_open = false;
  if (s_menu_choice == M_TODAY) today_win_open();
  else if (s_menu_choice == M_ALARMS) alarms_win_open();
  else if (s_menu_choice == M_EXIT) app_timer_register(0, exit_app, NULL);
}

// Coach stays up as the watch's clock, so leaving it is a menu choice rather than Back.
static void on_select_long(ClickRecognizerRef r, void *ctx) {
  if (s_menu_open) return;
  s_menu_open = true;
  s_menu_choice = -1;
  ActionMenuLevel *level = action_menu_level_create(3);
  action_menu_level_add_action(level, "Today", menu_perform, (void *)M_TODAY);
  action_menu_level_add_action(level, "Alarms", menu_perform, (void *)M_ALARMS);
  action_menu_level_add_action(level, "Exit ADHD Coach", menu_perform, (void *)M_EXIT);
  poster_open_menu(level, s_color, menu_closed);
}

static void on_back(ClickRecognizerRef r, void *ctx) {}  // home is the clock: Back stays here

static void refresh(void);
static void note_done(bool saved) { refresh(); }
static void on_down(ClickRecognizerRef r, void *ctx) { voice_note_start(note_done); }

#if COACH_DEMO
static void on_down_long(ClickRecognizerRef r, void *ctx) { coach_demo_reset(); }
#endif

static void clicks(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, on_select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, on_select_long, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, on_up);
  window_single_click_subscribe(BUTTON_ID_DOWN, on_down);
  window_single_click_subscribe(BUTTON_ID_BACK, on_back);
#if COACH_DEMO
  window_long_click_subscribe(BUTTON_ID_DOWN, 1500, on_down_long, NULL);
#endif
}

static void refresh(void) {
  if (!s_win || !window_stack_contains_window(s_win)) return;
  compute();
  layer_set_hidden(fittext_layer(s_name), s_state != H_NEXT && s_state != H_PARKED && s_state != H_FOCUS);
  layer_mark_dirty(s_canvas);
}

static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, draw);
  layer_add_child(root, s_canvas);
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_name = fittext_create(GRect(POSTER_TEXT_X, BLOCK_Y + 28, POSTER_TEXT_W, 32), f, f, 1, GColorWhite);
  layer_add_child(root, fittext_layer(s_name));
}

static void appear(Window *w) {
  refresh();
  fittext_restart(s_name);
}

static void unload(Window *w) {
  fittext_destroy(s_name);
  layer_destroy(s_canvas);
}

void home_win_open(void) {
  if (!s_win) {
    s_win = window_create();
    window_set_window_handlers(s_win, (WindowHandlers){ .load = load, .appear = appear, .unload = unload });
    window_set_click_config_provider(s_win, clicks);
    railtap_register(s_win, &(RailTaps){ on_up, on_select, on_down, on_select_long, NULL }, false);
  }
  window_stack_push(s_win, false);
}

bool home_win_on_top(void) { return s_win && window_stack_get_top_window() == s_win; }

void ui_refresh_all(void) {
  refresh();
  today_win_refresh();
  alarms_win_refresh();
  when_win_refresh();
}
