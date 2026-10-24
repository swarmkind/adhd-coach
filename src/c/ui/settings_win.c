#include <pebble.h>
#include "settings_win.h"
#include "poster.h"
#include "run_win.h"
#include "../platform/store.h"
#include "../platform/wake.h"
#include "../platform/sync.h"
#include "../platform/clock.h"
#include "../platform/buzz.h"
#include "../core/cc_time.h"
#include "../core/cc_focus.h"

#define ROW_H 52
#define TEXT_X 12

enum { R_LATER_TASK, R_LATER_REMINDER, R_LATER_EVENT, R_EVENT_TIME,
       R_FOCUS_WORK, R_FOCUS_SHORT, R_FOCUS_LONG, R_FOCUS_CYCLES, R_SOUNDS, R_SYNC, R_COUNT };

static const struct { const char *title; int16_t min, max, step; const char *unit; } ROWS[R_COUNT] = {
  [R_LATER_TASK] = { "Later for tasks", 5, 120, 5, "min" },
  [R_LATER_REMINDER] = { "Later for reminders", 5, 60, 5, "min" },
  [R_LATER_EVENT] = { "Later for events", 15, 240, 15, "min" },
  [R_EVENT_TIME] = { "Event reminders at", 6, 12, 1, ":00" },
  [R_FOCUS_WORK] = { "Focus", CC_FOCUS_WORK_MIN, CC_FOCUS_WORK_MAX, CC_FOCUS_WORK_STEP, "min" },
  [R_FOCUS_SHORT] = { "Short break", CC_FOCUS_SHORT_MIN, CC_FOCUS_SHORT_MAX, CC_FOCUS_SHORT_STEP, "min" },
  [R_FOCUS_LONG] = { "Long break", 5, 60, 5, "min" },
  [R_FOCUS_CYCLES] = { "Focus cycles", 1, 8, 1, "" },
  [R_SOUNDS] = { "Sounds", 0, 0, 0, "" },     // on / off, Select toggles
  [R_SYNC] = { "Phone sync", 0, 0, 0, "" },  // read-only
};

static Window *s_win;
static MenuLayer *s_menu;
static int s_first, s_editing;

static int get(int r) {
  switch (r) {
    case R_LATER_TASK: return g_cfg.later_task_min;
    case R_LATER_REMINDER: return g_cfg.later_reminder_min;
    case R_LATER_EVENT: return g_cfg.later_event_min;
    case R_EVENT_TIME: return g_cfg.event_time_min / 60;
    case R_FOCUS_WORK: return g_cfg.focus_work_min;
    case R_FOCUS_SHORT: return g_cfg.focus_short_min;
    case R_FOCUS_LONG: return g_cfg.focus_long_min;
    default: return g_cfg.focus_cycles;
  }
}

static void set(int r, int v) {
  switch (r) {
    case R_LATER_TASK: g_cfg.later_task_min = (uint8_t)v; break;
    case R_LATER_REMINDER: g_cfg.later_reminder_min = (uint8_t)v; break;
    case R_LATER_EVENT: g_cfg.later_event_min = (uint8_t)v; break;
    case R_EVENT_TIME: g_cfg.event_time_min = (uint16_t)(v * 60); break;
    case R_FOCUS_WORK: g_cfg.focus_work_min = (uint8_t)v; break;
    case R_FOCUS_SHORT: g_cfg.focus_short_min = (uint8_t)v; break;
    case R_FOCUS_LONG: g_cfg.focus_long_min = (uint8_t)v; break;
    default: g_cfg.focus_cycles = (uint8_t)v; break;
  }
}

static bool is_focus(int r) { return r >= R_FOCUS_WORK && r <= R_FOCUS_CYCLES; }

// "today 14:02", "Fri 23 Oct", or "never".
static void sync_line(char *buf, int len) {
  uint32_t utc = sync_last_utc();
  if (!utc) { snprintf(buf, len, "never"); return; }
  cc_sec at = cc_local_from_utc((int32_t)utc, clock_tz_offset());
  if (cc_day_of(at) == cc_day_of(clock_now_local())) {
    char t[10];
    poster_fmt_time(cc_min_of(at), t, sizeof(t));
    snprintf(buf, len, "today %s", t);
  } else {
    poster_fmt_date(cc_day_of(at), buf, len);
  }
}

static void value_line(int r, char *buf, int len) {
  if (r == R_SYNC) { sync_line(buf, len); return; }
  if (r == R_SOUNDS) {
    snprintf(buf, len, "%s", !store_sounds_on() ? "off" : speaker_is_muted() ? "on, watch muted" : "on");
    return;
  }
  int v = get(r);
  if (r == R_EVENT_TIME) poster_fmt_time((uint16_t)(v * 60), buf, len);
  else if (r == R_FOCUS_CYCLES) snprintf(buf, len, "%d %s", v, v == 1 ? "cycle" : "cycles");
  else snprintf(buf, len, "%d min", v);
}

static uint16_t num_rows(MenuLayer *m, uint16_t section, void *ctx) { return R_COUNT; }
static int16_t row_h(MenuLayer *m, MenuIndex *i, void *ctx) { return ROW_H; }

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
  GRect b = layer_get_bounds(cell);
  bool hl = menu_cell_layer_is_highlighted(cell);
  GColor accent = poster_kind_color(is_focus(idx->row) ? POSTER_FOCUS : POSTER_NOTHING);
  graphics_context_set_fill_color(ctx, hl ? accent : GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  if (!hl) {
    graphics_context_set_fill_color(ctx, accent);
    graphics_fill_rect(ctx, GRect(0, 6, 4, b.size.h - 12), 0, GCornerNone);
  }
  int w = b.size.w - 2 * TEXT_X;
  char v[16];
  value_line(idx->row, v, sizeof(v));
  graphics_context_set_text_color(ctx, hl ? GColorBlack : GColorWhite);
  graphics_draw_text(ctx, ROWS[idx->row].title, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(TEXT_X, 0, w, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_text_color(ctx, hl ? GColorBlack : GColorLightGray);
  graphics_draw_text(ctx, v, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(TEXT_X, 27, w, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void edited(bool saved, int32_t value) {
  if (!saved) return;
  set(s_editing, (int)value);
  store_save_settings();
  sync_settings_changed();
  wake_rearm();  // Later lengths and event reminder times move what is armed
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void select_click(MenuLayer *m, MenuIndex *idx, void *ctx) {
  static CcNumSpec spec;
  int r = idx->row;
  if (r == R_SYNC) return;
  if (r == R_SOUNDS) {
    store_set_sounds(!store_sounds_on());
    if (store_sounds_on()) buzz_tick();  // a sample
    menu_layer_reload_data(m);
    return;
  }
  s_editing = r;
  spec = (CcNumSpec){ ROWS[r].step, ROWS[r].min, ROWS[r].max, 0, "" };
  strncpy(spec.unit, ROWS[r].unit, CC_UNIT_MAX - 1);
  number_win_open_setting(ROWS[r].title, get(r), &spec, edited);
}

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
  });
  menu_layer_set_click_config_onto_window(s_menu, w);
  menu_layer_set_selected_index(s_menu, (MenuIndex){ 0, (uint16_t)s_first }, MenuRowAlignTop, false);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void unload(Window *w) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
}

void settings_win_open(int first_row) {
  s_first = first_row >= 0 && first_row < R_COUNT ? first_row : 0;
  if (!s_win) {
    s_win = window_create();
    window_set_window_handlers(s_win, (WindowHandlers){ .load = load, .unload = unload });
  }
  if (window_stack_contains_window(s_win)) window_stack_remove(s_win, false);
  window_stack_push(s_win, true);
}
