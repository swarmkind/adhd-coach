#include <pebble.h>
#include "alarm_win.h"
#include "poster.h"
#include "railtap.h"
#include "../platform/buzz.h"
#include "../platform/store.h"
#include "../platform/clock.h"
#include "../platform/wake.h"

#define RING_MS 3000
#define RINGS 200  // about ten minutes; then it stops itself, as the watch's own alarms do

static Window *s_win;
static Layer *s_canvas;
static int s_index = -1;
static int s_rings;
static AppTimer *s_timer;

// Snooze or stop, then off the screen. A wakeup launch with nothing else up exits after.
static void finish(bool snooze) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
  vibes_cancel();
  speaker_stop();
  if (s_index >= 0 && s_index < g_alarms.n) {
    CcAlarm *a = &g_alarms.a[s_index];
    if (snooze) cc_alarm_snooze(a, clock_now_local());
    else cc_alarms_stop(&g_alarms, s_index, clock_now_local());
    store_save_alarms();
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "alarm %d: %s", s_index, snooze ? "snooze" : "stop");
  s_index = -1;
  window_stack_remove(s_win, true);
  wake_rearm();
}

static void ring(void *data) {
  s_timer = NULL;
  if (++s_rings > RINGS) { finish(false); return; }
  light_enable_interaction();
  buzz_alarm(s_index < g_alarms.n && g_alarms.a[s_index].mode == CC_ALARM_SOUND);
  s_timer = app_timer_register(RING_MS, ring, NULL);
}

static void on_snooze(ClickRecognizerRef r, void *ctx) { finish(true); }
static void on_stop(ClickRecognizerRef r, void *ctx) { finish(false); }

static void clicks(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, on_snooze);
  window_single_click_subscribe(BUTTON_ID_BACK, on_snooze);
  window_single_click_subscribe(BUTTON_ID_DOWN, on_stop);
}

static void draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor bg = poster_kind_color(POSTER_ALARM);
  poster_draw_background(ctx, b, bg);
  static const RailAction rail[3] = { { G_LATER, "snooze" }, { G_NONE, NULL }, { G_STOP, "stop" } };
  poster_draw_rail(ctx, b, bg, rail);
  if (s_index < 0 || s_index >= g_alarms.n) return;
  const CcAlarm *a = &g_alarms.a[s_index];
  char t[8], d[24];
  poster_fmt_clock(a->at_min, t, sizeof(t));
  if (clock_is_24h_style()) snprintf(d, sizeof(d), "%s", cc_days_name(a->days));
  else snprintf(d, sizeof(d), "%s · %s", a->at_min < 720 ? "am" : "pm", cc_days_name(a->days));
  graphics_context_set_text_color(ctx, GColorBlack);
  poster_text(ctx, "Alarm", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(POSTER_TEXT_X, 6, POSTER_TEXT_W, 30));
  graphics_draw_text(ctx, t, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS), GRect(POSTER_TEXT_X, 52, POSTER_TEXT_W, 48),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  poster_text(ctx, d, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(POSTER_TEXT_X, 102, POSTER_TEXT_W, 30));
}

static void load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, draw);
  layer_add_child(root, s_canvas);
}

static void unload(Window *w) { layer_destroy(s_canvas); }

void alarm_win_ring(int index) {
  if (s_index >= 0 || index >= g_alarms.n) return;
  if (!s_win) {
    s_win = window_create();
    window_set_window_handlers(s_win, (WindowHandlers){ .load = load, .unload = unload });
    window_set_click_config_provider(s_win, clicks);
    // A tap can snooze, never stop: a pillow or a sleeve must not end an alarm.
    railtap_register(s_win, &(RailTaps){ on_snooze, NULL, NULL, NULL, NULL }, false);
  }
  s_index = index;
  s_rings = 0;
  APP_LOG(APP_LOG_LEVEL_INFO, "alarm %d: ringing", index);
  if (window_stack_contains_window(s_win)) window_stack_remove(s_win, false);
  window_stack_push(s_win, true);
  ring(NULL);
}
