#include <pebble.h>
#include "store.h"
#include "clock.h"
#include "sample.h"
#include "../config.h"
#include "../core/cc_codec.h"
#include "../core/cc_ring.h"

// Persist keys
#define K_SETTINGS 1
#define K_SCHEDULE 2
#define K_DAY 10        // 10..12
#define K_RUN 20
#define K_FOCUS 22
#define K_LAST 30       // 30..33
#define K_LOG_RING 40
#define K_HIST_RING 41
#define K_SYNC 45
#define K_SOUNDS 46     // watch-only, not synced
#define K_ALARMS 47     // watch-only, not synced
#define K_TODOS 48      // watch-only, not synced
#define K_ITEM 100      // 100..139
#define K_STEP 1000     // 1000 + slot * 16 + step
#define K_HIST 4000     // 4000..4089
#define K_LOG 5000      // 5000..5199

#define DAY_CHUNKS ((CC_MAX_ITEMS + CC_DAY_CHUNK - 1) / CC_DAY_CHUNK)
#define LAST_MAX (4 * CC_LOG_PER_CHUNK)
#define LOG_SLOTS 200
#define HIST_SLOTS 90

CcItem *g_items;
int g_nitems;
CcStep (*g_steps)[CC_MAX_STEPS];
CcDay *g_day;
CcRun g_run;
CcSettings g_cfg = CC_SETTINGS_DEFAULT;
CcFocus g_focus;
CcAlarms g_alarms;
CcTodos *g_todos;

static CcLogEntry *s_last;  // [LAST_MAX] last value per (item, step), on the heap like the items
static int s_nlast;
static CcRing s_logs, s_hist;
static uint8_t *s_buf;  // [PERSIST_DATA_MAX_LENGTH] on the heap, from store_load

static int read_key(uint32_t key) {
  return persist_exists(key) ? persist_read_data(key, s_buf, PERSIST_DATA_MAX_LENGTH) : -1;
}

static void write_key(uint32_t key, int len) {
  if (len < 0) { APP_LOG(APP_LOG_LEVEL_ERROR, "pack failed for key %d", (int)key); return; }
  int st = persist_write_data(key, s_buf, len);
  if (st < 0) APP_LOG(APP_LOG_LEVEL_ERROR, "persist write %d failed: %d", (int)key, st);
}

static void save_ring(uint32_t key, const CcRing *r) {
  s_buf[0] = CC_CODEC_V1;
  s_buf[1] = r->head & 0xFF; s_buf[2] = r->head >> 8;
  s_buf[3] = r->count & 0xFF; s_buf[4] = r->count >> 8;
  write_key(key, 5);
}

static void load_ring(uint32_t key, CcRing *r, uint16_t cap) {
  r->head = r->count = 0;
  if (read_key(key) != 5 || s_buf[0] != CC_CODEC_V1) return;
  r->head = (uint16_t)(s_buf[1] | s_buf[2] << 8);
  r->count = (uint16_t)(s_buf[3] | s_buf[4] << 8);
  if (r->head >= cap || r->count > cap) r->head = r->count = 0;
}

static bool load_schedule(void) {
  if (read_key(K_SCHEDULE) < 2 || s_buf[0] != CC_CODEC_V1 || s_buf[1] > CC_MAX_ITEMS) return false;
  int n = s_buf[1], k = 0;
  for (int i = 0; i < n; i++) {
    int len = read_key(K_ITEM + i);
    if (len <= 0 || !cc_unpack_item(s_buf, len, &g_items[k])) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "item slot %d unreadable, skipped", i);
      continue;
    }
    for (int j = 0; j < g_items[k].step_count; j++) {
      len = read_key(K_STEP + i * 16 + j);
      if (len <= 0 || !cc_unpack_step(s_buf, len, &g_steps[k][j])) { g_items[k].step_count = (uint8_t)j; break; }
    }
    k++;
  }
  g_nitems = k;
  return true;
}

void store_save_schedule(void) {
  for (int i = 0; i < g_nitems; i++) {
    write_key(K_ITEM + i, cc_pack_item(&g_items[i], s_buf, PERSIST_DATA_MAX_LENGTH));
    for (int j = 0; j < g_items[i].step_count; j++)
      write_key(K_STEP + i * 16 + j, cc_pack_step(&g_steps[i][j], s_buf, PERSIST_DATA_MAX_LENGTH));
  }
  for (int i = g_nitems; i < CC_MAX_ITEMS; i++)
    if (persist_exists(K_ITEM + i)) persist_delete(K_ITEM + i);
  s_buf[0] = CC_CODEC_V1;
  s_buf[1] = (uint8_t)g_nitems;
  write_key(K_SCHEDULE, 2);  // header last: a half-written schedule keeps the old count
}

static void load_day(void) {
  memset(g_day, 0, sizeof(*g_day));
  for (int c = 0; c < DAY_CHUNKS; c++) {
    int len = read_key(K_DAY + c);
    if (len <= 0 || !cc_unpack_day_chunk(s_buf, len, g_day)) break;
  }
}

void store_save_day(void) {
  for (int c = 0; c < DAY_CHUNKS; c++) {
    int len = cc_pack_day_chunk(g_day, c, s_buf, PERSIST_DATA_MAX_LENGTH);
    if (len >= 0) write_key(K_DAY + c, len);
    else if (persist_exists(K_DAY + c)) persist_delete(K_DAY + c);
  }
}

static void load_run(void) {
  int len = read_key(K_RUN);
  if (len <= 0 || !cc_unpack_run(s_buf, len, &g_run)) {
    memset(&g_run, 0, sizeof(g_run));
    g_run.step_end = CC_NEVER;
  }
}

void store_save_run(void) { write_key(K_RUN, cc_pack_run(&g_run, s_buf, PERSIST_DATA_MAX_LENGTH)); }
static void load_focus(void) {
  int len = read_key(K_FOCUS);
  if (len <= 0 || !cc_unpack_focus(s_buf, len, &g_focus)) {
    memset(&g_focus, 0, sizeof(g_focus));
    g_focus.phase_end = CC_NEVER;
  }
}

void store_save_focus(void) { write_key(K_FOCUS, cc_pack_focus(&g_focus, s_buf, PERSIST_DATA_MAX_LENGTH)); }
// Alarms are the watch's own, kept as the struct itself.
static void load_todos(void) { cc_todos_read(g_todos, s_buf, read_key(K_TODOS)); }
void store_save_todos(void) {
  memcpy(s_buf, g_todos, sizeof(*g_todos));
  write_key(K_TODOS, sizeof(*g_todos));
}
static void load_alarms(void) { cc_alarms_read(&g_alarms, s_buf, read_key(K_ALARMS)); }
void store_save_alarms(void) {
  memcpy(s_buf, &g_alarms, sizeof(g_alarms));
  write_key(K_ALARMS, sizeof(g_alarms));
}
void store_save_settings(void) { write_key(K_SETTINGS, cc_pack_settings(&g_cfg, s_buf, PERSIST_DATA_MAX_LENGTH)); }

static void load_last(void) {
  s_nlast = 0;
  for (int k = 0; k < LAST_MAX / CC_LOG_PER_CHUNK; k++) {
    int len = read_key(K_LAST + k);
    int n = len > 0 ? cc_unpack_logs(s_buf, len, &s_last[s_nlast], CC_LOG_PER_CHUNK) : -1;
    if (n <= 0) break;
    s_nlast += n;
  }
}

static void save_last(int idx) {
  int k = idx / CC_LOG_PER_CHUNK, first = k * CC_LOG_PER_CHUNK;
  int n = s_nlast - first < CC_LOG_PER_CHUNK ? s_nlast - first : CC_LOG_PER_CHUNK;
  write_key(K_LAST + k, cc_pack_logs(&s_last[first], n, s_buf, PERSIST_DATA_MAX_LENGTH));
}

void store_log_number(uint16_t item, uint8_t step, int32_t value) {
  CcLogEntry e = { (int32_t)time(NULL), item, step, value };

  int idx = -1, oldest = 0;
  for (int i = 0; i < s_nlast && idx < 0; i++) {
    if (s_last[i].item == item && s_last[i].step == step) idx = i;
    else if (s_last[i].utc < s_last[oldest].utc) oldest = i;
  }
  if (idx < 0) idx = s_nlast < LAST_MAX ? s_nlast++ : oldest;
  s_last[idx] = e;
  save_last(idx);

  // Append to the newest log chunk, or start a new one (overwriting the oldest when full).
  CcLogEntry chunk[CC_LOG_PER_CHUNK];
  int n = CC_LOG_PER_CHUNK;
  uint16_t slot = 0;
  if (s_logs.count) {
    slot = cc_ring_slot(&s_logs, LOG_SLOTS, s_logs.count - 1);
    int len = read_key(K_LOG + slot);
    n = len > 0 ? cc_unpack_logs(s_buf, len, chunk, CC_LOG_PER_CHUNK) : CC_LOG_PER_CHUNK;
    if (n < 0) n = CC_LOG_PER_CHUNK;  // unreadable: start a fresh chunk rather than overwrite it
  }
  if (n >= CC_LOG_PER_CHUNK) {
    slot = cc_ring_push(&s_logs, LOG_SLOTS);
    n = 0;
    save_ring(K_LOG_RING, &s_logs);
  }
  chunk[n++] = e;
  write_key(K_LOG + slot, cc_pack_logs(chunk, n, s_buf, PERSIST_DATA_MAX_LENGTH));
}

bool store_last_number(uint16_t item, uint8_t step, int32_t *value, int32_t *utc) {
  for (int i = 0; i < s_nlast; i++) {
    if (s_last[i].item == item && s_last[i].step == step) {
      *value = s_last[i].value;
      *utc = s_last[i].utc;
      return true;
    }
  }
  return false;
}

void store_history_push(const CcOutcome *o, int n, int32_t day) {
  if (n <= 0) return;
  uint16_t slot = cc_ring_push(&s_hist, HIST_SLOTS);
  save_ring(K_HIST_RING, &s_hist);
  write_key(K_HIST + slot, cc_pack_history(day, o, n, s_buf, PERSIST_DATA_MAX_LENGTH));
}

void store_load(void) {
  if (!g_steps) g_steps = calloc(CC_MAX_ITEMS, sizeof(*g_steps));  // ~27 KB; fails only with no heap at all
  if (!g_items) g_items = calloc(CC_MAX_ITEMS, sizeof(*g_items));
  if (!g_day) g_day = calloc(1, sizeof(*g_day));
  if (!g_todos) g_todos = calloc(1, sizeof(*g_todos));
  if (!s_last) s_last = calloc(LAST_MAX, sizeof(*s_last));
  if (!s_buf) s_buf = malloc(PERSIST_DATA_MAX_LENGTH);
  APP_LOG(APP_LOG_LEVEL_INFO, "persist max %d", (int)persist_get_max_size());
  int len = read_key(K_SETTINGS);
  if (len <= 0 || !cc_unpack_settings(s_buf, len, &g_cfg)) g_cfg = (CcSettings)CC_SETTINGS_DEFAULT;
#if COACH_DEMO
  g_cfg.later_task_min = 2;  // demo build: Laters come back within minutes
  g_cfg.later_reminder_min = 2;
  g_cfg.focus_work_min = g_cfg.focus_short_min = g_cfg.focus_long_min = 1;  // a phase change a minute
#endif
  if (load_schedule()) {
    APP_LOG(APP_LOG_LEVEL_INFO, "schedule read: %d items", g_nitems);
  } else {
    sample_load(clock_now_local());
    APP_LOG(APP_LOG_LEVEL_INFO, "sample schedule written: %d items", g_nitems);
  }
  load_day();
  load_run();
  load_focus();
  load_alarms();
  load_todos();
  load_last();
  load_ring(K_LOG_RING, &s_logs, LOG_SLOTS);
  load_ring(K_HIST_RING, &s_hist, HIST_SLOTS);
}

void store_load_sync(CcSync *s) {
  int len = read_key(K_SYNC);
  if (len <= 0 || !cc_unpack_sync(s_buf, len, s)) memset(s, 0, sizeof(*s));
}

void store_save_sync(const CcSync *s) { write_key(K_SYNC, cc_pack_sync(s, s_buf, PERSIST_DATA_MAX_LENGTH)); }

bool store_sounds_on(void) { return persist_exists(K_SOUNDS) ? persist_read_bool(K_SOUNDS) : true; }
void store_set_sounds(bool on) { persist_write_bool(K_SOUNDS, on); }

static int read_into(uint32_t key, uint8_t *buf, int len) {
  return persist_exists(key) ? persist_read_data(key, buf, len) : -1;
}

int store_log_chunks(void) { return s_logs.count; }
int store_log_chunk(int i, uint8_t *buf, int len) {
  if (i < 0 || i >= s_logs.count) return -1;
  return read_into(K_LOG + cc_ring_slot(&s_logs, LOG_SLOTS, (uint16_t)i), buf, len);
}

int store_hist_days(void) { return s_hist.count; }
int store_hist_record(int i, uint8_t *buf, int len) {
  if (i < 0 || i >= s_hist.count) return -1;
  return read_into(K_HIST + cc_ring_slot(&s_hist, HIST_SLOTS, (uint16_t)i), buf, len);
}
