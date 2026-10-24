#include <pebble.h>
#include "focus_win.h"
#include "poster.h"
#include "fittext.h"
#include "home_win.h"
#include "settings_win.h"
#include "railtap.h"
#include "../coach.h"
#include "../platform/store.h"
#include "../platform/clock.h"
#include "../platform/wake.h"
#include "../platform/sync.h"
#include "../core/cc_time.h"

enum { M_ADD5, M_STOP, M_LENGTHS, M_MARK, M_NOT_YET };

static Window *s_win;
static Layer *s_canvas;
static FitText *s_label;
static AppTimer *s_timer;
static uint16_t s_label_id;   // the label for a session not started yet
static uint16_t s_prompt_id;  // a finished or stopped session's label, waiting on "mark done"
static bool s_menu_open;
static int s_menu_choice;     // -1 = none
static char s_mark[CC_NAME_MAX + 12];

// The setup screen's length rows, everything left of the rail.
static const CcBox WORK_BOX = { 0, 70, PBL_DISPLAY_WIDTH - POSTER_RAIL_W, 64 };
static const CcBox BREAK_BOX = { 0, 134, PBL_DISPLAY_WIDTH - POSTER_RAIL_W, 56 };

static uint16_t label(void) { return g_focus.active ? g_focus.label_id : s_label_id; }

static void save(void) {
  store_save_focus();
  wake_rearm();  // also re-arms the phase-end timer in wake.c, which advances the session
  ui_refresh_all();
}

static void refresh(void);

static void menu_perform(ActionMenu *menu, const ActionMenuItem *action, void *ctx) {
  s_menu_choice = (int)action_menu_item_get_action_data(action);
}

static void open_menu(ActionMenuLevel *level, ActionMenuDidCloseCb closed) {
  s_menu_open = true;
  s_menu_choice = -1;
  ActionMenuConfig cfg = {
    .root_level = level,
    .colors = { .background = poster_kind_color(POSTER_FOCUS), .foreground = GColorBlack },
    .align = ActionMenuAlignCenter,
    .did_close = closed,
  };
  action_menu_open(&cfg);
}

static void prompt_closed(ActionMenu *menu, const ActionMenuItem *performed, void *ctx) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
  s_menu_open = false;
  if (s_menu_choice >= 0) {  // answered; Back leaves it for the next time Focus is in front
    if (s_menu_choice == M_MARK && coach_item(s_prompt_id)) coach_apply(s_prompt_id, CC_A_DONE, 0);
    s_prompt_id = 0;
  }
}

// After a session with a label ends: offer to mark that task done.
static void prompt(void *data) {
  const CcItem *it = coach_item(s_prompt_id);
  if (!it) { s_prompt_id = 0; return; }
  if (s_menu_open || !focus_win_on_top()) return;  // asked again when Focus is next in front
  snprintf(s_mark, sizeof(s_mark), "Mark %s done", it->name);
  ActionMenuLevel *level = action_menu_level_create(2);
  action_menu_level_add_action(level, s_mark, menu_perform, (void *)M_MARK);
  action_menu_level_add_action(level, "Not yet", menu_perform, (void *)M_NOT_YET);
  open_menu(level, prompt_closed);
}

bool focus_win_on_top(void) { return s_win && window_stack_get_top_window() == s_win; }

void focus_win_finished(uint16_t lab) {
  s_label_id = 0;
  if (!lab) return;
  s_prompt_id = lab;
  if (focus_win_on_top()) app_timer_register(0, prompt, NULL);  // not from inside a click handler
}

// Before a session: a tap or Up/Down changes a length, which is the default from then on.
static void adjust(CcFocusTap tap) {
  if (g_focus.active || !cc_focus_adjust(&g_cfg, tap)) return;
  store_save_settings();
  sync_settings_changed();
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void setup_tap(int x, int y) { adjust(cc_focus_tap(WORK_BOX, BREAK_BOX, x, y)); }

static void tick(void *data) {  // redraw only; wake.c advances the session
  s_timer = NULL;
  refresh();
}

static void refresh(void) {
  if (!s_win || !s_canvas) return;
  const CcItem *it = coach_item(label());
  fittext_set(s_label, it ? it->name : "");
  layer_mark_dirty(s_canvas);
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
  if (g_focus.active && !g_focus.paused && focus_win_on_top()) s_timer = app_timer_register(1000, tick, NULL);
}

// One length row: minus on the left, plus on the right, the minutes between.
static void draw_length(GContext *ctx, CcBox box, int minutes, const char *font, int num_y, const char *unit, int unit_y) {
  char num[8];
  int mid = box.y + box.h / 2;
  poster_draw_glyph(ctx, G_MINUS, GPoint(box.x + 24, mid), GColorBlack);
  poster_draw_glyph(ctx, G_PLUS, GPoint(box.x + box.w - 24, mid), GColorBlack);
  snprintf(num, sizeof(num), "%d", minutes);
  graphics_draw_text(ctx, num, fonts_get_system_font(font), GRect(box.x + 40, box.y + num_y, box.w - 80, 50),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, unit, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(box.x + 36, box.y + unit_y, box.w - 72, 22),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor bg = poster_kind_color(POSTER_FOCUS);
  cc_sec now = clock_now_local();
  poster_draw_background(ctx, b, bg);

  char head[24], t[16], line[24];
  if (!g_focus.active) snprintf(head, sizeof(head), "Focus");
  else if (g_focus.phase == CC_FOCUS_WORK) snprintf(head, sizeof(head), "Focus %d of %d", g_focus.cycle, g_focus.cycles);
  else snprintf(head, sizeof(head), "%s", g_focus.phase == CC_FOCUS_SHORT ? "Short break" : "Long break");
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, head, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(POSTER_TEXT_X, 2, POSTER_TEXT_W, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  if (!g_focus.active) {  // setup: the lengths this session will use
    draw_length(ctx, WORK_BOX, g_cfg.focus_work_min, FONT_KEY_BITHAM_42_BOLD, -6, "min", 40);
    draw_length(ctx, BREAK_BOX, g_cfg.focus_short_min, FONT_KEY_GOTHIC_28_BOLD, -2, "min break", 28);
    if (g_cfg.focus_cycles == 1) snprintf(line, sizeof(line), "1 round");
    else snprintf(line, sizeof(line), "%d rounds", g_cfg.focus_cycles);
    graphics_draw_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(POSTER_TEXT_X, 194, POSTER_TEXT_W, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  } else {
    int32_t left = cc_focus_remaining(&g_focus, now);
    snprintf(t, sizeof(t), "%d:%02d", (int)(left / 60), (int)(left % 60));
    graphics_draw_text(ctx, t, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), GRect(POSTER_TEXT_X, 80, POSTER_TEXT_W, 50),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    if (g_focus.paused) {
      snprintf(line, sizeof(line), "paused");
    } else {
      poster_fmt_time(cc_min_of(g_focus.phase_end), t, sizeof(t));
      snprintf(line, sizeof(line), "ends %s", t);
    }
    graphics_draw_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(POSTER_TEXT_X, 132, POSTER_TEXT_W, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }

  RailAction rail[3] = { { G_PLUS, NULL }, { G_START, "start" }, { G_MINUS, NULL } };
  if (g_focus.active) {
    rail[0] = (RailAction){ G_NONE, NULL };
    rail[1] = (RailAction){ g_focus.paused ? G_RESUME : G_PAUSE, NULL };
    rail[2] = (RailAction){ G_SKIP, "next" };
  }
  poster_draw_rail(ctx, b, bg, rail);
}

static void on_select(ClickRecognizerRef r, void *ctx) {
  cc_sec now = clock_now_local();
  if (!g_focus.active) cc_focus_start(&g_focus, &g_cfg, s_label_id, now);
  else cc_focus_toggle_pause(&g_focus, now);
  save();
  refresh();
}

static void on_up(ClickRecognizerRef r, void *ctx) { adjust(CC_TAP_WORK_MORE); }

static void on_down(ClickRecognizerRef r, void *ctx) {  // no buzz on a button press
  if (!g_focus.active) { adjust(CC_TAP_WORK_LESS); return; }
  uint16_t lab = g_focus.label_id;
  CcFocusEvent ev = cc_focus_skip(&g_focus, clock_now_local());
  save();
  if (ev == CC_FOCUS_FINISHED) focus_win_finished(lab);
  refresh();
}

static void menu_closed(ActionMenu *menu, const ActionMenuItem *performed, void *ctx) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
  s_menu_open = false;
  uint16_t lab = g_focus.label_id;
  switch (s_menu_choice) {
    case M_ADD5: cc_focus_add(&g_focus, 5, clock_now_local()); save(); break;
    case M_STOP: cc_focus_stop(&g_focus); save(); focus_win_finished(lab); break;
    case M_LENGTHS: settings_win_open(SETTINGS_ROW_FOCUS); break;
  }
  refresh();
}

static void on_select_long(ClickRecognizerRef r, void *ctx) {
  if (s_menu_open) return;
  ActionMenuLevel *level = action_menu_level_create(3);
  if (g_focus.active) {
    action_menu_level_add_action(level, "+5 min", menu_perform, (void *)M_ADD5);
    action_menu_level_add_action(level, "Stop", menu_perform, (void *)M_STOP);
  }
  action_menu_level_add_action(level, "Lengths", menu_perform, (void *)M_LENGTHS);
  open_menu(level, menu_closed);
}

static void clicks(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, on_select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, on_select_long, NULL);
  window_single_click_subscribe(BUTTON_ID_UP, on_up);
  window_single_click_subscribe(BUTTON_ID_DOWN, on_down);
}

static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, draw);
  layer_add_child(root, s_canvas);
  s_label = fittext_create(GRect(POSTER_TEXT_X, 34, POSTER_TEXT_W, 32), fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                           fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 1, GColorBlack);
  layer_add_child(root, fittext_layer(s_label));
}

// Catch up a phase that ended while the app was closed, and ask a pending mark-done.
static void appear(Window *w) {
  wake_focus_check();
  if (s_prompt_id) app_timer_register(0, prompt, NULL);
  refresh();
}

static void disappear(Window *w) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
}

static void unload(Window *w) {
  fittext_destroy(s_label);
  s_label = NULL;
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

void focus_win_open(uint16_t label_id) {
  if (!g_focus.active) s_label_id = label_id;
  if (!s_win) {
    s_win = window_create();
    window_set_window_handlers(s_win, (WindowHandlers){ .load = load, .appear = appear, .disappear = disappear, .unload = unload });
    window_set_click_config_provider(s_win, clicks);
    railtap_register(s_win, &(RailTaps){ on_up, on_select, on_down, on_select_long, setup_tap }, false);
  }
  if (window_stack_get_top_window() == s_win) { refresh(); return; }
  if (window_stack_contains_window(s_win)) window_stack_remove(s_win, false);
  window_stack_push(s_win, true);
}
