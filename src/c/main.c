#include <pebble.h>
#include "config.h"
#include "coach.h"
#include "platform/store.h"
#include "platform/clock.h"
#include "platform/wake.h"
#include "platform/glance.h"
#include "platform/voice.h"
#include "platform/sync.h"
#include "ui/home_win.h"
#include "ui/railtap.h"
#include "core/cc_state.h"

static int32_t s_tz;

static void prv_minute(struct tm *t, TimeUnits changed) {
  coach_minute();
  int32_t tz = clock_tz_offset();
  if (tz != s_tz) {  // travel or DST: every local time moved
    APP_LOG(APP_LOG_LEVEL_INFO, "time zone %d -> %d", (int)s_tz, (int)tz);
    s_tz = tz;
    wake_rearm();
  }
  wake_push_due();
}

static void prv_wakeup(WakeupId id, int32_t cookie) {
  APP_LOG(APP_LOG_LEVEL_INFO, "wakeup %d while open", (int)id);
  wake_process_due();
}

static void exit_app(void *data) { window_stack_pop_all(false); }
static void quick_note_done(bool saved) { app_timer_register(600, exit_app, NULL); }  // after the buzz
static void quick_note(void *data) { voice_note_start(quick_note_done); }

// Quick Launch, holding Down on the watch face: straight into a voice note.
static bool quick_note_launch(void) {
  return launch_reason() == APP_LAUNCH_QUICK_LAUNCH && launch_get_quick_launch_action() == APP_QUICK_LAUNCH_ACTION_HOLD &&
         launch_button() == BUTTON_ID_DOWN;
}

static void prv_init(void) {
  store_load();
  sync_init();
  s_tz = clock_tz_offset();
  cc_sec now = clock_now_local();
  coach_ensure_today(now);
  if (cc_sweep(&g_day, g_items, g_nitems, now, &g_cfg)) store_save_day();
  bool by_wakeup = launch_reason() == APP_LAUNCH_WAKEUP;
  APP_LOG(APP_LOG_LEVEL_INFO, "launch reason %d", (int)launch_reason());
  if (!by_wakeup) home_win_open();
  wake_process_due();
  // Launched by a wakeup with nothing due after all: no window, so the app exits quietly.
  if (by_wakeup && !window_stack_get_top_window()) APP_LOG(APP_LOG_LEVEL_INFO, "wakeup: nothing due");
  if (quick_note_launch() && home_win_on_top()) app_timer_register(400, quick_note, NULL);  // no alert due
  app_touch_navigation_enable(true);  // lists, menus and transcripts scroll and tap by touch
  railtap_init();                     // and the rail on Coach's own screens
  wakeup_service_subscribe(prv_wakeup);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_minute);
}

static void prv_deinit(void) {
  railtap_deinit();
  tick_timer_service_unsubscribe();
  wake_rearm_on_exit();
  glance_update();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
