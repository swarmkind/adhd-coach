#include <pebble.h>
#include "when_win.h"
#include "poster.h"
#include "railtap.h"
#include "../platform/clock.h"
#include "../core/cc_time.h"
#include "../core/cc_todo.h"

#define TEXT_X 12
#define DAY_Y 28
#define TIME_Y 100

static Window *s_win;
static Layer *s_canvas;
static cc_sec s_at;
static int s_field;
static WhenDone s_done;

// The day, how far off it is, then the time; Up and Down change the underlined one.
static void draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor bg = poster_kind_color(POSTER_VOICE);
  poster_draw_background(ctx, b, bg);
  RailAction rail[3] = { { G_PLUS, NULL }, { G_START, "next" }, { G_MINUS, NULL } };
  if (s_field == CC_PICK_MIN) rail[1] = (RailAction){ G_DONE, "save" };
  poster_draw_rail(ctx, b, bg, rail);
  poster_draw_editor_head(ctx, "Remind me");
  cc_sec now = clock_now_local();
  char line[24];

  graphics_context_set_fill_color(ctx, GColorBlack);
  int days = (int)(cc_day_of(s_at) - cc_day_of(now));
  if (days == 0) snprintf(line, sizeof(line), "Today");
  else if (days == 1) snprintf(line, sizeof(line), "Tomorrow");
  else poster_fmt_date(cc_day_of(s_at), line, sizeof(line));
  poster_draw_field(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), GRect(TEXT_X, DAY_Y, POSTER_TEXT_W, 34),
                    GTextAlignmentLeft, s_field == CC_PICK_DAY);
  int mins = (int)((s_at - now + 59) / 60);
  if (days >= 2) snprintf(line, sizeof(line), "in %d days", days);
  else if (days == 0 && mins >= 60) snprintf(line, sizeof(line), "in %d h %d min", mins / 60, mins % 60);
  else if (days == 0) snprintf(line, sizeof(line), "in %d min", mins);
  else line[0] = 0;
  poster_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(TEXT_X, DAY_Y + 42, POSTER_TEXT_W, 22));
  poster_draw_editor_clock(ctx, cc_min_of(s_at), TIME_Y,
                           s_field == CC_PICK_HOUR ? POSTER_CLOCK_HOUR : s_field == CC_PICK_MIN ? POSTER_CLOCK_MIN : 0);
}

static void step(int dir) {
  s_at = cc_todo_pick(s_at, s_field, dir, clock_now_local());
  layer_mark_dirty(s_canvas);
}
static void on_up(ClickRecognizerRef r, void *ctx) { step(1); }
static void on_down(ClickRecognizerRef r, void *ctx) { step(-1); }

static void on_select(ClickRecognizerRef r, void *ctx) {
  if (s_field < CC_PICK_MIN) {
    s_field++;
    layer_mark_dirty(s_canvas);
    return;
  }
  WhenDone done = s_done;
  cc_sec at = cc_todo_pick(s_at, CC_PICK_DAY, 0, clock_now_local());  // not in the past, if the clock moved on
  window_stack_remove(s_win, true);
  if (done) done(at);
}

static void on_back(ClickRecognizerRef r, void *ctx) {
  if (s_field == CC_PICK_DAY) { window_stack_remove(s_win, true); return; }
  s_field--;
  layer_mark_dirty(s_canvas);
}

// A tap on the day, the hour or the minutes underlines it; the rail's + and - change it.
static void on_tap(int x, int y) {
  s_field = y < TIME_Y ? CC_PICK_DAY : x < POSTER_CLOCK_SPLIT_X ? CC_PICK_HOUR : CC_PICK_MIN;
  layer_mark_dirty(s_canvas);
}

static void clicks(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, on_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, on_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, on_select);
  window_single_click_subscribe(BUTTON_ID_BACK, on_back);
}

static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, draw);
  layer_add_child(root, s_canvas);
}

static void unload(Window *w) { layer_destroy(s_canvas); }

void when_win_open(cc_sec at, WhenDone done) {
  s_at = cc_todo_pick(at, CC_PICK_DAY, 0, clock_now_local());
  s_field = CC_PICK_DAY;
  s_done = done;
  if (!s_win) {
    s_win = window_create();
    window_set_window_handlers(s_win, (WindowHandlers){ .load = load, .unload = unload });
    window_set_click_config_provider(s_win, clicks);
    railtap_register(s_win, &(RailTaps){ on_up, on_select, on_down, NULL, on_tap }, false);
  }
  window_stack_push(s_win, true);
}

void when_win_refresh(void) {
  if (!s_win || !window_stack_contains_window(s_win)) return;
  s_at = cc_todo_pick(s_at, CC_PICK_DAY, 0, clock_now_local());
  layer_mark_dirty(s_canvas);
}
