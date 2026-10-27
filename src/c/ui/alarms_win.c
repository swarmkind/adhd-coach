#include <pebble.h>
#include "alarms_win.h"
#include "poster.h"
#include "railtap.h"
#include "../platform/store.h"
#include "../platform/clock.h"
#include "../platform/wake.h"
#include "../core/cc_time.h"

#define ROW_H 52
#define HEAD_H 34
#define TEXT_X 12
#define TIME_Y 40
#define DAYS_Y 104
#define MODE_Y 150

enum { F_HOUR, F_MIN, F_DAYS, F_MODE };
enum { M_TOGGLE, M_CHANGE, M_DELETE, M_CUSTOM, M_IN };  // M_IN + k: in IN_MIN[k] minutes
static const uint8_t IN_MIN[] = { 5, 10, 15, 30, 60 };
static char s_in_label[ARRAY_LENGTH(IN_MIN)][28];

static Window *s_list_win, *s_edit_win;
static MenuLayer *s_menu;
static Layer *s_canvas;
static int s_edit;  // the alarm being changed, or -1 for a new one
static int s_min, s_days_i, s_mode, s_field;
static int s_menu_row, s_menu_choice;

static void changed(void) {
  store_save_alarms();
  wake_rearm();
}

// The editor: Up and Down change the underlined field, Select moves on and saves after the last.

static void edit_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor bg = poster_kind_color(POSTER_ALARM);
  poster_draw_background(ctx, b, bg);
  RailAction rail[3] = { { G_PLUS, NULL }, { G_START, "next" }, { G_MINUS, NULL } };
  if (s_field == F_MODE) rail[1] = (RailAction){ G_DONE, "save" };
  poster_draw_rail(ctx, b, bg, rail);
  poster_draw_editor_head(ctx, s_edit < 0 ? "New alarm" : "Alarm");
  poster_draw_editor_clock(ctx, s_min, TIME_Y,
                           s_field == F_HOUR ? POSTER_CLOCK_HOUR : s_field == F_MIN ? POSTER_CLOCK_MIN : 0);
  poster_draw_field(ctx, cc_days_name(cc_days_preset(s_days_i)), fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                    GRect(TEXT_X, DAYS_Y, POSTER_TEXT_W, 34), GTextAlignmentLeft, s_field == F_DAYS);
  poster_draw_field(ctx, s_mode == CC_ALARM_SOUND ? "Sound + vibrate" : "Vibrate only", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                    GRect(TEXT_X, MODE_Y, POSTER_TEXT_W, 30), GTextAlignmentLeft, s_field == F_MODE);
  if (s_mode == CC_ALARM_SOUND && speaker_is_muted())  // the alarm would only vibrate
    poster_text(ctx, "The watch is muted", fonts_get_system_font(FONT_KEY_GOTHIC_18),
                GRect(TEXT_X, MODE_Y + 40, POSTER_TEXT_W, 22));
}

static void step(int dir) {
  if (s_field == F_HOUR) s_min = (s_min + dir * 60 + 1440) % 1440;
  else if (s_field == F_MIN) s_min = s_min / 60 * 60 + (s_min % 60 + dir + 60) % 60;
  else if (s_field == F_DAYS) s_days_i = (s_days_i + dir + CC_DAYS_PRESETS) % CC_DAYS_PRESETS;
  else s_mode = !s_mode;
  layer_mark_dirty(s_canvas);
}
static void edit_up(ClickRecognizerRef r, void *ctx) { step(1); }
static void edit_down(ClickRecognizerRef r, void *ctx) { step(-1); }

static void edit_select(ClickRecognizerRef r, void *ctx) {
  if (s_field < F_MODE) {
    s_field++;
    layer_mark_dirty(s_canvas);
    return;
  }
  int i = s_edit;
  if (i < 0) {
    if (g_alarms.n >= CC_ALARM_MAX) return;
    i = g_alarms.n++;
  }
  cc_alarm_set(&g_alarms.a[i], (uint16_t)s_min, cc_days_preset(s_days_i), clock_now_local());
  g_alarms.a[i].mode = (uint8_t)s_mode;
  changed();
  window_stack_remove(s_edit_win, true);
}

static void edit_back(ClickRecognizerRef r, void *ctx) {
  if (s_field == F_HOUR) { window_stack_remove(s_edit_win, true); return; }
  s_field--;
  layer_mark_dirty(s_canvas);
}

// A tap on the hour, the minutes, the days or the sound underlines it; the rail's + and - change it.
static void edit_tap(int x, int y) {
  s_field = y >= MODE_Y ? F_MODE : y >= DAYS_Y ? F_DAYS : x < POSTER_CLOCK_SPLIT_X ? F_HOUR : F_MIN;
  layer_mark_dirty(s_canvas);
}

static void edit_clicks(void *ctx) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, edit_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, edit_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, edit_select);
  window_single_click_subscribe(BUTTON_ID_BACK, edit_back);
}

static void edit_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, edit_draw);
  layer_add_child(root, s_canvas);
}

static void edit_unload(Window *w) { layer_destroy(s_canvas); }

static void edit_open(int i) {
  s_edit = i;
  s_field = F_HOUR;
  s_min = i < 0 ? 7 * 60 : g_alarms.a[i].at_min;
  s_days_i = i < 0 ? 1 : cc_days_index(g_alarms.a[i].days);  // new: every day, with sound
  s_mode = i < 0 ? CC_ALARM_SOUND : g_alarms.a[i].mode;
  if (!s_edit_win) {
    s_edit_win = window_create();
    window_set_window_handlers(s_edit_win, (WindowHandlers){ .load = edit_load, .unload = edit_unload });
    window_set_click_config_provider(s_edit_win, edit_clicks);
    railtap_register(s_edit_win, &(RailTaps){ edit_up, edit_select, edit_down, NULL, edit_tap }, false);
  }
  window_stack_push(s_edit_win, true);
}

// The list: one row per alarm, then "New alarm" while there is room.

static uint16_t num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  return (uint16_t)(g_alarms.n + (g_alarms.n < CC_ALARM_MAX));
}
static int16_t row_h(MenuLayer *m, MenuIndex *i, void *ctx) { return ROW_H; }
static int16_t head_h(MenuLayer *m, uint16_t section, void *ctx) { return HEAD_H; }

static void draw_head(GContext *ctx, const Layer *cell, uint16_t section, void *data) {  // the time now
  char t[12], line[16];
  poster_fmt_time(cc_min_of(clock_now_local()), t, sizeof(t));
  snprintf(line, sizeof(line), "Now %s", t);
  graphics_context_set_text_color(ctx, GColorLightGray);
  poster_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
              GRect(TEXT_X, 0, layer_get_bounds(cell).size.w - 2 * TEXT_X, 30));
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
  GColor accent = poster_kind_color(POSTER_ALARM);
  if (idx->row >= g_alarms.n) { poster_draw_row(ctx, cell, accent, false, "New alarm", NULL); return; }
  const CcAlarm *a = &g_alarms.a[idx->row];
  char t[12], d[36];
  poster_fmt_time(a->at_min, t, sizeof(t));
  snprintf(d, sizeof(d), "%s · %s%s", cc_days_name(a->days), a->mode == CC_ALARM_VIBRATE ? "vibrate" : "sound",
           a->on ? "" : " · off");
  poster_draw_row(ctx, cell, accent, a->on, t, d);
}

static void menu_perform(ActionMenu *menu, const ActionMenuItem *action, void *ctx) {
  s_menu_choice = (int)action_menu_item_get_action_data(action);
}

static void menu_closed(ActionMenu *menu, const ActionMenuItem *performed, void *ctx) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
  CcAlarm *a = &g_alarms.a[s_menu_row];
  if (s_menu_choice == M_CHANGE) { edit_open(s_menu_row); return; }
  if (s_menu_choice == M_DELETE) cc_alarm_remove(&g_alarms, s_menu_row);
  else if (s_menu_choice == M_TOGGLE && a->on) { a->on = 0; a->snooze = 0; }
  else if (s_menu_choice == M_TOGGLE) cc_alarm_set(a, a->at_min, a->days, clock_now_local());  // from now on
  else return;
  changed();
  if (s_menu) menu_layer_reload_data(s_menu);
}

// New alarm: in so many minutes, to the second, or a time and days of your own.
static void new_closed(ActionMenu *menu, const ActionMenuItem *performed, void *ctx) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
  if (s_menu_choice == M_CUSTOM) { edit_open(-1); return; }
  if (s_menu_choice < M_IN || g_alarms.n >= CC_ALARM_MAX) return;
  CcAlarm *a = &g_alarms.a[g_alarms.n++];
  memset(a, 0, sizeof(*a));  // with sound
  cc_alarm_set_in(a, IN_MIN[s_menu_choice - M_IN], clock_now_local());
  changed();
  if (s_menu) menu_layer_reload_data(s_menu);
}

static void open_menu(ActionMenuLevel *level, ActionMenuDidCloseCb closed) {
  poster_open_menu(level, poster_kind_color(POSTER_ALARM), closed);
}

static void select_click(MenuLayer *m, MenuIndex *idx, void *ctx) {
  s_menu_choice = -1;
  if (idx->row >= g_alarms.n) {
    ActionMenuLevel *level = action_menu_level_create(ARRAY_LENGTH(IN_MIN) + 1);
    cc_sec now = clock_now_local();
    for (unsigned k = 0; k < ARRAY_LENGTH(IN_MIN); k++) {
      char t[12];
      poster_fmt_time(cc_min_of(now + IN_MIN[k] * 60), t, sizeof(t));
      if (IN_MIN[k] == 60) snprintf(s_in_label[k], sizeof(s_in_label[k]), "In 1 hour · %s", t);
      else snprintf(s_in_label[k], sizeof(s_in_label[k]), "In %d min · %s", IN_MIN[k], t);
      action_menu_level_add_action(level, s_in_label[k], menu_perform, (void *)(M_IN + k));
    }
    action_menu_level_add_action(level, "Custom", menu_perform, (void *)M_CUSTOM);
    open_menu(level, new_closed);
    return;
  }
  s_menu_row = idx->row;
  ActionMenuLevel *level = action_menu_level_create(3);
  action_menu_level_add_action(level, g_alarms.a[s_menu_row].on ? "Turn off" : "Turn on", menu_perform, (void *)M_TOGGLE);
  action_menu_level_add_action(level, "Change", menu_perform, (void *)M_CHANGE);
  action_menu_level_add_action(level, "Delete", menu_perform, (void *)M_DELETE);
  open_menu(level, menu_closed);
}

static void list_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  window_set_background_color(w, GColorBlack);
  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_normal_colors(s_menu, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_menu, GColorBlack, GColorBlack);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = num_rows,
    .get_cell_height = row_h,
    .get_header_height = head_h,
    .draw_header = draw_head,
    .draw_row = draw_row,
    .select_click = select_click,
  });
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void list_appear(Window *w) { menu_layer_reload_data(s_menu); }  // back from the editor

static void list_unload(Window *w) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
}

void alarms_win_refresh(void) {  // each minute: the time now
  if (s_menu) menu_layer_reload_data(s_menu);
  if (s_edit_win && window_stack_contains_window(s_edit_win)) layer_mark_dirty(s_canvas);
}

void alarms_win_open(void) {
  if (!s_list_win) {
    s_list_win = window_create();
    window_set_window_handlers(s_list_win, (WindowHandlers){ .load = list_load, .appear = list_appear, .unload = list_unload });
  }
  if (window_stack_contains_window(s_list_win)) window_stack_remove(s_list_win, false);
  window_stack_push(s_list_win, true);
}
