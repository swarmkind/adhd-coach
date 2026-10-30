#include <pebble.h>
#include "run_win.h"
#include "poster.h"
#include "railtap.h"
#include "fittext.h"
#include "../coach.h"
#include "../platform/store.h"
#include "../platform/clock.h"
#include "../platform/buzz.h"
#include "../platform/wake.h"
#include "../core/cc_run.h"
#include "../core/cc_num.h"
#include "../core/cc_fires.h"
#include "../core/cc_time.h"
#include "../core/cc_steps.h"

// ---- number entry, shared by number steps and the number window ----

typedef struct {
  uint16_t item;
  uint8_t step;
  CcNumSpec spec;
  int32_t value;
  bool has_last;
  int32_t last, last_utc;
} NumState;

static void num_init(NumState *n, uint16_t item, uint8_t step, const CcNumSpec *spec) {
  n->item = item;
  n->step = step;
  n->spec = *spec;
  n->has_last = item != CC_ITEM_NONE && store_last_number(item, step, &n->last, &n->last_utc);
  n->value = cc_num_step(n->has_last ? n->last : 0, spec, 0, 0);  // clamped into range
}

static uint16_t repeats_of(ClickRecognizerRef r) {  // r is NULL for a tap
  return r && click_recognizer_is_repeating(r) ? click_number_of_clicks_counted(r) : 0;
}

static void num_change(NumState *n, int dir, ClickRecognizerRef r) {
  n->value = cc_num_step(n->value, &n->spec, dir, repeats_of(r));
}

static void num_save(const NumState *n) {
  if (n->item != CC_ITEM_NONE) store_log(n->item, n->step, n->value, true);
  buzz_tick();
}

// Value box at `y`, then "last 82.7 on Tue" under it.
static void num_draw(GContext *ctx, const NumState *n, int y) {
  GRect box = GRect(POSTER_TEXT_X, y, POSTER_TEXT_W, 62);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, box, 6, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_round_rect(ctx, grect_inset(box, GEdgeInsets(1)), 6);

  char v[16];
  cc_num_format(n->value, n->spec.decimals, v, sizeof(v));
  GFont big = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  GFont unit = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GSize vs = graphics_text_layout_get_content_size(v, big, GRect(0, 0, 400, 60), GTextOverflowModeFill, GTextAlignmentLeft);
  GSize us = graphics_text_layout_get_content_size(n->spec.unit, unit, GRect(0, 0, 100, 30), GTextOverflowModeFill,
                                                   GTextAlignmentLeft);
  if (vs.w + 4 + us.w > box.size.w - 10) {  // very long values: drop to a smaller face
    big = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    vs = graphics_text_layout_get_content_size(v, big, GRect(0, 0, 400, 60), GTextOverflowModeFill, GTextAlignmentLeft);
  }
  int x = box.origin.x + (box.size.w - (vs.w + 4 + us.w)) / 2;
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, v, big, GRect(x, y + 4, vs.w + 2, 50), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, n->spec.unit, unit, GRect(x + vs.w + 4, y + 28, us.w + 2, 24), GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);

  if (!n->has_last) return;
  char last[12], line[32], day[12];
  cc_num_format(n->last, n->spec.decimals, last, sizeof(last));
  time_t t = n->last_utc;
  if (cc_day_of(cc_local_from_utc(n->last_utc, clock_tz_offset())) == cc_day_of(clock_now_local())) {
    snprintf(day, sizeof(day), "today");
  } else {
    char wd[6];
    strftime(wd, sizeof(wd), "%a", localtime(&t));
    snprintf(day, sizeof(day), "on %s", wd);
  }
  snprintf(line, sizeof(line), "last %s %s", last, day);
  poster_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(POSTER_TEXT_X, y + 68, POSTER_TEXT_W, 24));
}

// ---- done screen ----

static Window *s_done_win;
static Layer *s_done_canvas;
static FitText *s_done_next;
static AppTimer *s_done_timer;
static char s_done_routine[CC_NAME_MAX];
static char s_done_when[24];
static GColor s_done_next_color;
static int s_done_anytime = -1;  // slot of an anytime task to offer, or -1
static bool s_done_has_next;

static void done_close(void *data) {
  s_done_timer = NULL;
  window_stack_remove(s_done_win, true);
}

static void done_find_next(void) {
  cc_sec now = clock_now_local();
  CcFire f[16];
  int n = cc_collect(g_items, g_nitems, g_day, CC_NEVER, CC_NEVER, now, &g_cfg, f, 16);
  s_done_has_next = false;
  for (int i = 0; i < n; i++) {
    if (f[i].kind != CC_FIRE_DUE) continue;
    const CcItem *it = coach_item(f[i].id);
    if (!it) continue;
    char t[12];
    poster_fmt_time(cc_min_of(f[i].at), t, sizeof(t));
    int32_t days = cc_day_of(f[i].at) - cc_day_of(now);
    if (days == 0) snprintf(s_done_when, sizeof(s_done_when), "%s", t);
    else if (days == 1) snprintf(s_done_when, sizeof(s_done_when), "tomorrow %s", t);
    else snprintf(s_done_when, sizeof(s_done_when), "in %d days, %s", (int)days, t);
    fittext_set(s_done_next, it->name);
    s_done_next_color = poster_kind_color(it->kind);
    s_done_has_next = true;
    break;
  }
  s_done_anytime = cc_anytime_next(g_items, g_nitems, g_day);
}

static void done_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor bg = poster_kind_color(CC_ROUTINE);
  poster_draw_background(ctx, b, bg);
  graphics_context_set_text_color(ctx, GColorBlack);
  poster_text(ctx, "Done", fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), GRect(POSTER_TEXT_X, 14, POSTER_TEXT_W, 50));
  poster_text(ctx, s_done_routine, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
              GRect(POSTER_TEXT_X, 66, POSTER_TEXT_W, 24));

  // the next thing, in a black block
  int w = b.size.w - POSTER_RAIL_W;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 112, w, 92), 0, GCornerNone);
  if (s_done_has_next) {
    graphics_context_set_text_color(ctx, s_done_next_color);
    poster_text(ctx, "Next", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                GRect(POSTER_TEXT_X, 116, POSTER_TEXT_W, 22));
    graphics_context_set_text_color(ctx, GColorWhite);
    poster_text(ctx, s_done_when, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                GRect(POSTER_TEXT_X, 172, POSTER_TEXT_W, 22));
  } else {
    graphics_context_set_text_color(ctx, GColorWhite);
    poster_text(ctx, "All done for today", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                GRect(POSTER_TEXT_X, 140, POSTER_TEXT_W, 30));
  }

  RailAction rail[3] = { { G_NONE, NULL }, { G_NONE, NULL }, { G_NONE, NULL } };
  if (s_done_anytime >= 0) rail[1] = (RailAction){ G_START, "start" };
  poster_draw_rail(ctx, b, bg, rail);
  if (s_done_anytime >= 0) {  // what "start" would start
    graphics_context_set_text_color(ctx, GColorBlack);
    char line[CC_NAME_MAX + 8];
    snprintf(line, sizeof(line), "or: %s", g_items[s_done_anytime].name);
    poster_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(POSTER_TEXT_X, 204, POSTER_TEXT_W, 22));
  }
}

static void done_select(ClickRecognizerRef r, void *ctx) {
  if (s_done_anytime < 0) return;
  coach_apply(g_items[s_done_anytime].id, CC_A_START, 0);
  done_close(NULL);
}

static void done_back(ClickRecognizerRef r, void *ctx) { done_close(NULL); }

static void done_clicks(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, done_select);
  window_single_click_subscribe(BUTTON_ID_BACK, done_back);
}

static void done_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_done_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_done_canvas, done_draw);
  layer_add_child(root, s_done_canvas);
  s_done_next = fittext_create(GRect(POSTER_TEXT_X, 136, POSTER_TEXT_W, 32), fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                               fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 1, GColorWhite);
  layer_add_child(root, fittext_layer(s_done_next));
  done_find_next();
  layer_set_hidden(fittext_layer(s_done_next), !s_done_has_next);
  s_done_timer = app_timer_register(6000, done_close, NULL);
}

static void done_unload(Window *w) {
  if (s_done_timer) { app_timer_cancel(s_done_timer); s_done_timer = NULL; }
  fittext_destroy(s_done_next);
  layer_destroy(s_done_canvas);
}

static void done_open(const char *routine) {
  strncpy(s_done_routine, routine, sizeof(s_done_routine) - 1);
  if (!s_done_win) {
    s_done_win = window_create();
    window_set_window_handlers(s_done_win, (WindowHandlers){ .load = done_load, .unload = done_unload });
    window_set_click_config_provider(s_done_win, done_clicks);
    railtap_register(s_done_win, &(RailTaps){ NULL, done_select, NULL, NULL, NULL }, false);
  }
  window_stack_push(s_done_win, true);
}

// ---- step screen ----

static Window *s_step_win;
static Layer *s_step_canvas;
static FitText *s_step_title;
static AppTimer *s_step_timer;
static NumState s_num;
static bool s_value_dirty;  // g_run.value changed since the run was last saved
static const CcNumSpec QTY_SPEC = { 1, 0, 9999, 0, "" };
static char s_swap_label[CC_NAME_MAX + 8];
static int s_slot = -1;
static bool s_menu_open;
static int s_menu_choice;

enum { M_SWAP, M_LEAVE, M_FINISH, M_SKIP };

static const CcStep *steps(void) { return coach_steps(s_slot); }
static const CcStep *cur(void) { return cc_run_cur(&g_run, steps()); }
static uint8_t cur_index(void) { return g_run.order[g_run.step]; }  // among the item's steps, for logs
static const CcStep *other(void) { return cc_run_other(&g_run, &g_items[s_slot], steps()); }
static bool has_qty(const CcStep *s) { return s->type == CC_STEP_MANUAL && (s->flags & CC_SF_QTY); }
static bool has_weight(const CcStep *s) { return s->type == CC_STEP_MANUAL && (s->flags & CC_SF_WEIGHT); }

// A Do it step's quantity and weight, logged as it is done.
static void log_done(const CcStep *s) {
  uint16_t id = g_items[s_slot].id;
  if (has_qty(s)) store_log(id, cur_index(), cc_run_qty(&g_run, s), false);
  if (has_weight(s)) store_log(id, cur_index() | CC_LOG_WEIGHT, cc_ramp_at(&s->weight, g_run.day), false);
}

// A timer that steps up logs the length it ran, so History shows the climb; Undo takes it back.
static bool logs_length(const CcStep *s) { return s->type == CC_STEP_TIMED && s->amount.inc; }
static void log_timer(const CcStep *s, uint8_t index) {
  if (logs_length(s)) store_log(g_items[s_slot].id, index, cc_ramp_at(&s->amount, g_run.day), false);
}
static bool undo_offered(cc_sec now) { return g_run.advanced_at && now - g_run.advanced_at <= CC_UNDO_S; }

static void finish_run(bool done) {
  cc_sec now = clock_now_local();
  const CcItem *it = &g_items[s_slot];
  bool counts = cc_run_counts_for_today(&g_run, cc_day_of(now));
  g_run.active = 0;
  g_run.step_end = CC_NEVER;
  store_save_run();
  wake_rearm();
  if (counts) coach_apply(it->id, done ? CC_A_DONE : CC_A_SKIP, 0);
  window_stack_remove(s_step_win, false);
  if (done) done_open(it->name);
}

static void schedule_tick(void);

static void enter_step(void) {
  const CcStep *s = cur();
  fittext_set(s_step_title, s->name);
  if (s->type == CC_STEP_NUMBER) {
    num_init(&s_num, g_items[s_slot].id, cur_index(), &s->num);
    if (g_run.value != CC_RUN_PLAN) s_num.value = cc_num_step(g_run.value, &s->num, 0, 0);  // changed before a menu or Leave
  }
  layer_mark_dirty(s_step_canvas);
  schedule_tick();
}

// After a step change from a button: save, then finish or show the new step.
static void after_change(CcRunEvent ev) {
  store_save_run();
  wake_rearm();
  if (ev == CC_RUN_FINISHED) finish_run(true);
  else enter_step();
}

static void step_tick(void *data) {
  s_step_timer = NULL;
  if (!g_run.active) return;
  const CcStep *was = cur();
  uint8_t index = cur_index();
  CcRunEvent ev = cc_run_tick(&g_run, steps(), clock_now_local());
  if (ev != CC_RUN_NONE) {
    log_timer(was, index);
    buzz_step_done();
    after_change(ev);
    return;
  }
  layer_mark_dirty(s_step_canvas);
  schedule_tick();
}

// A 1 s tick while a clock runs or the undo offer is showing.
static void schedule_tick(void) {
  if (s_step_timer) { app_timer_cancel(s_step_timer); s_step_timer = NULL; }
  bool timed = g_run.step_end != CC_NEVER && !g_run.paused;
  if (g_run.active && (timed || undo_offered(clock_now_local())))
    s_step_timer = app_timer_register(1000, step_tick, NULL);
}

static void fmt_remaining(int32_t s, char *buf, int len) {
  if (s < 0) s = 0;
  if (s > 99 * 3600) s = 99 * 3600;
  if (s >= 3600) snprintf(buf, len, "%d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
  else snprintf(buf, len, "%d:%02d", (int)(s / 60), (int)(s % 60));
}

static int text_w(const char *t, GFont f) {
  return graphics_text_layout_get_content_size(t, f, GRect(0, 0, 400, 60), GTextOverflowModeFill, GTextAlignmentLeft).w;
}

static void draw_cross(GContext *ctx, int x, int mid, int size) {  // Gothic has no ×
  graphics_draw_line(ctx, GPoint(x, mid - size / 2), GPoint(x + size, mid + size / 2));
  graphics_draw_line(ctx, GPoint(x, mid + size / 2), GPoint(x + size, mid - size / 2));
}

// "× 21", "21 × 12.5 kg" or "12.5 kg" at `y`, in a smaller face when it doesn't fit.
static void target_draw(GContext *ctx, const CcStep *s, int y) {
  bool hq = has_qty(s), hw = has_weight(s);
  char q[8], w[12];
  snprintf(q, sizeof(q), "%d", (int)cc_run_qty(&g_run, s));
  cc_num_format(cc_ramp_at(&s->weight, g_run.day), s->num.decimals, w, sizeof(w));
  GFont unit = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  int uw = hw && s->num.unit[0] ? text_w(s->num.unit, unit) + 4 : 0;
  for (int small = 0; small < 2; small++) {
    GFont big = fonts_get_system_font(small ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_BITHAM_42_BOLD);
    int cross = small ? 10 : 16, gap = small ? 5 : 7, mid = y + (small ? 19 : 26), unit_dy = small ? 10 : 24;
    int qw = hq ? text_w(q, big) : 0, ww = hw ? text_w(w, big) : 0;
    int total = qw + ww + uw + (hq ? cross + gap * (hw ? 2 : 1) : 0);
    if (total > POSTER_TEXT_W && !small) continue;
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, small ? 2 : 3);
    int x = POSTER_TEXT_X;
    if (hq && !hw) { draw_cross(ctx, x, mid, cross); x += cross + gap; }
    if (hq) { graphics_draw_text(ctx, q, big, GRect(x, y, qw + 2, 50), GTextOverflowModeFill, GTextAlignmentLeft, NULL); x += qw; }
    if (hq && hw) { draw_cross(ctx, x + gap, mid, cross); x += cross + 2 * gap; }
    if (hw) {
      graphics_draw_text(ctx, w, big, GRect(x, y, ww + 2, 50), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
      if (uw) graphics_draw_text(ctx, s->num.unit, unit, GRect(x + ww + 4, y + unit_dy, uw, 24), GTextOverflowModeFill,
                                 GTextAlignmentLeft, NULL);
    }
    return;
  }
}

static void step_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor bg = poster_kind_color(CC_ROUTINE);
  cc_sec now = clock_now_local();
  const CcStep *s = cur();
  poster_draw_background(ctx, b, bg);
  poster_draw_back_hint(ctx, "skip");

  char pos[12];
  snprintf(pos, sizeof(pos), "%d of %d", g_run.step + 1, g_run.step_count);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, pos, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(POSTER_TEXT_X, 3, POSTER_TEXT_W, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // progress: done black, current white, future orange
  int n = g_run.step_count, gap = n > 12 ? 1 : 3;
  int seg = (POSTER_TEXT_W - gap * (n - 1)) / n;
  for (int i = 0; i < n; i++) {
    graphics_context_set_fill_color(ctx, i < g_run.step ? GColorBlack : i == g_run.step ? GColorWhite : GColorOrange);
    graphics_fill_rect(ctx, GRect(POSTER_TEXT_X + i * (seg + gap), 24, seg, 6), 0, GCornerNone);
  }

  if (s->type == CC_STEP_TIMED) {
    char t[16], line[24];
    fmt_remaining(cc_run_remaining(&g_run, now), t, sizeof(t));
    graphics_draw_text(ctx, t, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD), GRect(POSTER_TEXT_X, 80, POSTER_TEXT_W, 50),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    if (g_run.paused) {
      snprintf(line, sizeof(line), cc_run_waiting(&g_run) ? "Select to start" : "stopped");
    } else {
      poster_fmt_time(cc_min_of(g_run.step_end), t, sizeof(t));
      snprintf(line, sizeof(line), "ends %s", t);
    }
    poster_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(POSTER_TEXT_X, 132, POSTER_TEXT_W, 24));
  } else if (s->type == CC_STEP_NUMBER) {
    num_draw(ctx, &s_num, 76);
  } else if (has_qty(s) || has_weight(s)) {
    target_draw(ctx, s, 80);
  }

  const CcStep *alt = other();
  if (alt) {
    char line[CC_NAME_MAX + 6];
    snprintf(line, sizeof(line), "or: %s", alt->name);
    poster_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(POSTER_TEXT_X, 174, POSTER_TEXT_W, 24));
  }

  if (g_run.step + 1 < g_run.step_count && s->type != CC_STEP_NUMBER) {
    char next[CC_NAME_MAX + 8];
    snprintf(next, sizeof(next), "next: %s", steps()[g_run.order[g_run.step + 1]].name);
    poster_text(ctx, next, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(POSTER_TEXT_X, 196, POSTER_TEXT_W, 24));
  }

  RailAction rail[3] = { { G_NONE, NULL }, { G_DONE, "done" }, { G_NONE, NULL } };
  if (s->type == CC_STEP_TIMED) rail[1] = g_run.paused ? (RailAction){ G_RESUME, "start" } : (RailAction){ G_PAUSE, "stop" };
  if (s->type == CC_STEP_NUMBER || has_qty(s)) {
    rail[0] = (RailAction){ G_PLUS, NULL };
    rail[2] = (RailAction){ G_MINUS, NULL };
  }
  if (s->type == CC_STEP_NUMBER) rail[1] = (RailAction){ G_DONE, "save" };
  if (undo_offered(now)) rail[0] = (RailAction){ G_UNDO, "undo" };
  poster_draw_rail(ctx, b, bg, rail);
}

static void step_select(ClickRecognizerRef r, void *ctx) {
  cc_sec now = clock_now_local();
  switch (cur()->type) {
    case CC_STEP_TIMED:
      cc_run_toggle_pause(&g_run, now);
      store_save_run();
      wake_rearm();
      layer_mark_dirty(s_step_canvas);
      schedule_tick();
      break;
    case CC_STEP_NUMBER:
      num_save(&s_num);
      after_change(cc_run_next(&g_run, steps(), now));
      break;
    default:
      log_done(cur());
      after_change(cc_run_next(&g_run, steps(), now));
      break;
  }
}

// Up and Down on a Do it count or a number: the run keeps the value until the step changes.
static void value_change(int dir, ClickRecognizerRef r) {
  if (has_qty(cur())) {
    g_run.value = cc_num_step(cc_run_qty(&g_run, cur()), &QTY_SPEC, dir, repeats_of(r));
  } else if (cur()->type == CC_STEP_NUMBER) {
    num_change(&s_num, dir, r);
    g_run.value = s_num.value;
  } else {
    return;
  }
  s_value_dirty = true;
  layer_mark_dirty(s_step_canvas);
}

static void step_up(ClickRecognizerRef r, void *ctx) {
  cc_sec now = clock_now_local();
  if (!(r && click_recognizer_is_repeating(r)) && undo_offered(now) && cc_run_undo(&g_run, steps(), now)) {
    if (logs_length(cur())) store_unlog(g_items[s_slot].id, cur_index());  // it runs again and logs then
    after_change(CC_RUN_NONE);
    return;
  }
  value_change(+1, r);
}

static void step_down(ClickRecognizerRef r, void *ctx) { value_change(-1, r); }

static void step_back(ClickRecognizerRef r, void *ctx) {  // skip this step, nothing logged
  after_change(cc_run_next(&g_run, steps(), clock_now_local()));
}

static void menu_perform(ActionMenu *menu, const ActionMenuItem *action, void *ctx) {
  s_menu_choice = (int)action_menu_item_get_action_data(action);
}

static void menu_closed(ActionMenu *menu, const ActionMenuItem *performed, void *ctx) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
  s_menu_open = false;
  if (s_menu_choice == M_SWAP && cc_run_swap(&g_run, &g_items[s_slot], steps(), clock_now_local())) after_change(CC_RUN_NONE);
  else if (s_menu_choice == M_LEAVE) window_stack_remove(s_step_win, true);  // the run stays parked
  else if (s_menu_choice == M_FINISH) finish_run(true);
  else if (s_menu_choice == M_SKIP) finish_run(false);
}

static void step_select_long(ClickRecognizerRef r, void *ctx) {
  if (s_menu_open) return;
  const CcStep *alt = other();
  ActionMenuLevel *level = action_menu_level_create(alt ? 4 : 3);
  if (alt) {
    snprintf(s_swap_label, sizeof(s_swap_label), "Swap to %s", alt->name);
    action_menu_level_add_action(level, s_swap_label, menu_perform, (void *)M_SWAP);
  }
  action_menu_level_add_action(level, "Leave, resume later", menu_perform, (void *)M_LEAVE);
  action_menu_level_add_action(level, "Finish routine now", menu_perform, (void *)M_FINISH);
  action_menu_level_add_action(level, "Skip routine today", menu_perform, (void *)M_SKIP);
  s_menu_open = true;
  s_menu_choice = -1;
  poster_open_menu(level, poster_kind_color(CC_ROUTINE), menu_closed);
}

static void step_clicks(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, step_select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, step_select_long, NULL);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, CC_NUM_REPEAT_MS, step_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, CC_NUM_REPEAT_MS, step_down);
  window_single_click_subscribe(BUTTON_ID_BACK, step_back);
}

static void step_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_step_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_step_canvas, step_draw);
  layer_add_child(root, s_step_canvas);
  s_step_title = fittext_create(GRect(POSTER_TEXT_X, 34, POSTER_TEXT_W, 36), fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                                fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 1, GColorBlack);
  layer_add_child(root, fittext_layer(s_step_title));
}

static void close_dead_runner(void *ctx) {
  if (s_step_win) window_stack_remove(s_step_win, false);
}

static void step_appear(Window *w) {
  if (!g_run.active) { app_timer_register(0, close_dead_runner, NULL); return; }  // ended from Today or an alert
  // Catch up a clock that ran out while this window was hidden or the app was closed.
  const CcStep *was = cur();
  uint8_t index = cur_index();
  CcRunEvent ev = cc_run_tick(&g_run, steps(), clock_now_local());
  if (ev != CC_RUN_NONE) {
    log_timer(was, index);
    buzz_step_done();
    store_save_run();
    wake_rearm();
  }
  if (ev == CC_RUN_FINISHED) { finish_run(true); return; }
  enter_step();
}

static void step_disappear(Window *w) {
  if (s_step_timer) { app_timer_cancel(s_step_timer); s_step_timer = NULL; }
  if (s_value_dirty && g_run.active) store_save_run();  // once, not on every press
  s_value_dirty = false;
}

static void step_unload(Window *w) {
  fittext_destroy(s_step_title);
  s_step_title = NULL;
  layer_destroy(s_step_canvas);
}

bool run_win_showing(void) { return s_step_win && window_stack_contains_window(s_step_win); }

void run_win_open(void) {
  s_slot = coach_slot(g_run.item_id);
  if (!g_run.active || s_slot < 0 || g_run.step >= g_run.step_count) return;
  if (!s_step_win) {
    s_step_win = window_create();
    window_set_window_handlers(s_step_win, (WindowHandlers){
      .load = step_load, .appear = step_appear, .disappear = step_disappear, .unload = step_unload });
    window_set_click_config_provider(s_step_win, step_clicks);
    railtap_register(s_step_win, &(RailTaps){ step_up, step_select, step_down, step_select_long, NULL }, false);
  }
  if (!window_stack_contains_window(s_step_win)) window_stack_push(s_step_win, true);
}

// ---- standalone number window (reminders that ask for a number) ----

static Window *s_num_win;
static Layer *s_num_canvas;
static FitText *s_num_title;
static NumState s_wnum;
static void (*s_num_done)(bool saved, int32_t value);
static const char *s_num_setting;  // title of the setting being edited, NULL for an item

static void num_win_close(bool saved) {
  void (*done)(bool, int32_t) = s_num_done;
  int32_t v = s_wnum.value;
  window_stack_remove(s_num_win, false);
  if (done) done(saved, v);
}

static void num_win_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  const CcItem *it = coach_item(s_wnum.item);
  GColor bg = s_num_setting ? poster_kind_color(POSTER_NOTHING) : poster_kind_color(it ? it->kind : CC_REMINDER);
  poster_draw_background(ctx, b, bg);
  poster_draw_back_hint(ctx, s_num_setting ? "cancel" : "skip");
  num_draw(ctx, &s_wnum, 76);
  static const RailAction rail[3] = { { G_PLUS, NULL }, { G_DONE, "save" }, { G_MINUS, NULL } };
  poster_draw_rail(ctx, b, bg, rail);
}

static void num_win_select(ClickRecognizerRef r, void *ctx) { num_save(&s_wnum); num_win_close(true); }
static void num_win_back(ClickRecognizerRef r, void *ctx) { num_win_close(false); }
static void num_win_up(ClickRecognizerRef r, void *ctx) { num_change(&s_wnum, +1, r); layer_mark_dirty(s_num_canvas); }
static void num_win_down(ClickRecognizerRef r, void *ctx) { num_change(&s_wnum, -1, r); layer_mark_dirty(s_num_canvas); }

static void num_win_clicks(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, num_win_select);
  window_single_click_subscribe(BUTTON_ID_BACK, num_win_back);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, CC_NUM_REPEAT_MS, num_win_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, CC_NUM_REPEAT_MS, num_win_down);
}

static void num_win_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_num_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_num_canvas, num_win_draw);
  layer_add_child(root, s_num_canvas);
  s_num_title = fittext_create(GRect(POSTER_TEXT_X, 30, POSTER_TEXT_W, 36), fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                               fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 1, GColorBlack);
  layer_add_child(root, fittext_layer(s_num_title));
  const CcItem *it = coach_item(s_wnum.item);
  fittext_set(s_num_title, s_num_setting ? s_num_setting : it ? it->name : "");
}

static void num_win_unload(Window *w) {
  fittext_destroy(s_num_title);
  layer_destroy(s_num_canvas);
}

static void num_win_push(void) {
  if (!s_num_win) {
    s_num_win = window_create();
    window_set_window_handlers(s_num_win, (WindowHandlers){ .load = num_win_load, .unload = num_win_unload });
    window_set_click_config_provider(s_num_win, num_win_clicks);
    railtap_register(s_num_win, &(RailTaps){ num_win_up, num_win_select, num_win_down, NULL, NULL }, false);
  }
  window_stack_push(s_num_win, true);
}

void number_win_open_setting(const char *title, int32_t value, const CcNumSpec *spec,
                             void (*done)(bool saved, int32_t value)) {
  num_init(&s_wnum, CC_ITEM_NONE, CC_STEP_NONE, spec);
  s_wnum.value = cc_num_step(value, spec, 0, 0);
  s_num_setting = title;
  s_num_done = done;
  num_win_push();
}

void number_win_open(uint16_t item, uint8_t step, const CcNumSpec *spec, void (*done)(bool saved, int32_t value)) {
  num_init(&s_wnum, item, step, spec);
  s_num_setting = NULL;
  s_num_done = done;
  num_win_push();
}
