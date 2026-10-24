#include "railtap.h"
#include "poster.h"
#include "../core/cc_touch.h"

#define MAX_WINDOWS 8
#define HOLD_MS 500
#define RAIL_X (PBL_DISPLAY_WIDTH - POSTER_RAIL_W - 6)  // a little slack for a fingertip
#define SPLIT_UP ((POSTER_Y_UP + POSTER_Y_SELECT) / 2)
#define SPLIT_DOWN ((POSTER_Y_SELECT + POSTER_Y_DOWN) / 2)

static struct { Window *w; RailTaps taps; } s_reg[MAX_WINDOWS];
static int s_n;
static CcTapTrack s_track;
static AppTimer *s_hold;
static Window *s_hold_win;
static bool s_held;  // the hold fired: the rest of this touch is used up

static const RailTaps *taps_for(Window *w) {
  for (int i = 0; i < s_n; i++)
    if (s_reg[i].w == w) return &s_reg[i].taps;
  return NULL;
}

void railtap_register(Window *w, const RailTaps *taps, bool keep_bridge) {
  if (!taps_for(w) && s_n < MAX_WINDOWS) {
    s_reg[s_n].w = w;
    s_reg[s_n].taps = *taps;
    s_n++;
  }
  if (!keep_bridge) window_set_touch_bridge_disabled(w, true);  // a tap is never also a system button press
}

static void cancel_hold(void) {
  if (s_hold) { app_timer_cancel(s_hold); s_hold = NULL; }
}

static void hold(void *data) {
  s_hold = NULL;
  Window *top = window_stack_get_top_window();
  const RailTaps *t = taps_for(top);
  if (!s_track.down || s_track.moved || top != s_hold_win || !t || !t->select_long) return;
  s_held = true;
  t->select_long(NULL, NULL);
}

static void on_touch(const TouchEvent *e, void *ctx) {
  if (e->non_navigational) return;  // stray contact while the watch was idle
  int type = e->type == TouchEvent_Touchdown ? CC_TOUCH_DOWN : e->type == TouchEvent_Liftoff ? CC_TOUCH_UP : CC_TOUCH_MOVE;
  int x, y;
  bool tap = cc_tap_feed(&s_track, type, e->x, e->y, &x, &y);
  Window *top = window_stack_get_top_window();
  const RailTaps *t = taps_for(top);
  if (type == CC_TOUCH_DOWN) {
    cancel_hold();
    s_held = false;
    if (t && t->select_long && cc_rail_zone(e->x, e->y, RAIL_X, SPLIT_UP, SPLIT_DOWN) == CC_RAIL_SELECT) {
      s_hold_win = top;
      s_hold = app_timer_register(HOLD_MS, hold, NULL);
    }
    return;
  }
  if (type == CC_TOUCH_UP) cancel_hold();
  if (!tap || s_held || !t) return;
  switch (cc_rail_zone(x, y, RAIL_X, SPLIT_UP, SPLIT_DOWN)) {
    case CC_RAIL_UP: if (t->up) t->up(NULL, NULL); break;
    case CC_RAIL_SELECT: if (t->select) t->select(NULL, NULL); break;
    case CC_RAIL_DOWN: if (t->down) t->down(NULL, NULL); break;
    default: if (t->body) t->body(x, y); break;
  }
}

void railtap_init(void) { touch_service_subscribe(on_touch, NULL); }

void railtap_deinit(void) {
  cancel_hold();
  touch_service_unsubscribe();
}
