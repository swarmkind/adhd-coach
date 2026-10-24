#include <pebble.h>
#include "glance.h"
#include "store.h"
#include "clock.h"
#include "../coach.h"
#include "../core/cc_fires.h"
#include "../core/cc_time.h"

#define SLICES 4
#define FIRES_MAX 16

// "Drink water · 17:00". Braces would start a template, so names lose them.
static void subtitle(const CcItem *it, cc_sec at, char *buf, int len) {
  char name[CC_NAME_MAX], t[12];
  int k = 0;
  for (const char *p = it->name; *p && k < CC_NAME_MAX - 1; p++)
    if (*p != '{' && *p != '}') name[k++] = *p;
  name[k] = 0;
  int m = cc_min_of(at), h = m / 60;
  if (clock_is_24h_style()) snprintf(t, sizeof(t), "%02d:%02d", h, m % 60);
  else snprintf(t, sizeof(t), "%d:%02d %s", h % 12 ? h % 12 : 12, m % 60, h < 12 ? "am" : "pm");
  snprintf(buf, len, "%s · %s", name, t);
}

static void reload(AppGlanceReloadSession *session, size_t limit, void *ctx) {
  static char text[SLICES][CC_NAME_MAX + 16];
  cc_sec now = clock_now_local();
  int32_t tz = clock_tz_offset();
  CcFire f[FIRES_MAX];
  int n = cc_collect(g_items, g_nitems, &g_day, CC_NEVER, CC_NEVER, now, &g_cfg, f, FIRES_MAX);
  size_t added = 0;
  for (int i = 0; i < n && added < limit && added < SLICES; i++) {
    const CcItem *it = f[i].kind == CC_FIRE_DUE ? coach_item(f[i].id) : NULL;
    if (!it || f[i].at <= now) continue;
    subtitle(it, f[i].at, text[added], sizeof(text[added]));
    AppGlanceSlice s = {
      .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = text[added] },
      .expiration_time = (time_t)(f[i].at - tz),  // shown until that alert comes due
    };
    if (app_glance_add_slice(session, s) != APP_GLANCE_RESULT_SUCCESS) break;
    added++;
  }
  if (!added && limit) {
    AppGlanceSlice s = {
      .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = "All done for today" },
      .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION,
    };
    app_glance_add_slice(session, s);
  }
}

void glance_update(void) { app_glance_reload(reload, NULL); }
