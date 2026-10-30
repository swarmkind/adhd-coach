#include <pebble.h>
#include "sync.h"
#include "store.h"
#include "notes.h"
#include "wake.h"
#include "glance.h"
#include "clock.h"
#include "../coach.h"
#include "../core/cc_sync.h"
#include "../core/cc_steps.h"
#include "../core/cc_codec.h"
#include "../ui/home_win.h"
#include "../ui/today_win.h"

enum { C_HELLO = 1, C_UP, C_UP_END, C_ACK, C_SCHED, C_SCHED_END, C_OPS, C_DONE };
#define DATA_MAX 900
#define QUIET_MS 12000
#define RETRY_MS 500

typedef enum { P_SCHED, P_SETTINGS, P_LOGS, P_HIST, P_NOTES, P_TOMBS, P_END } Phase;

// One upload, on the heap while it runs.
typedef struct {
  Phase phase;
  bool full_sched;
  int item, step;                   // P_SCHED: step -1 is the item itself
  int log_i, nlogs, hist_i, nhist;  // the next log chunk and history day to send
  uint32_t ids[NOTES_SLOTS];
  int nnotes, note_i, part, parts;  // P_NOTES: part 0 is the header
  CcNote note;
  char text[CC_NOTE_TEXT_MAX + 1];
  uint8_t rec[PERSIST_DATA_MAX_LENGTH];
  int rec_len;
  uint8_t rec_type;                 // 0 = nothing waiting
  uint8_t out[DATA_MAX];
  int out_len, msgs;
} Upload;

// A schedule from the phone, kept apart until all of it is in.
typedef struct {
  CcItem items[CC_MAX_ITEMS];
  CcStep pool[CC_POOL_MAX];      // each item's steps from its step_first, given as it arrives
  uint64_t mask[CC_MAX_ITEMS];   // the step records seen, by index
  int n, used;
  bool bad, have_cfg;
  CcSettings cfg;
  uint32_t cfg_utc;
} Stage;

static CcSync s_sync;
static uint32_t s_hello_phone;
static Upload *s_up;
static Stage *s_stage;
static AppTimer *s_quiet, *s_retry;
static bool s_sending;
static int32_t s_inflight;           // CMD of the message in the outbox
static int s_fails;
static int32_t s_reply = INT32_MIN;  // a DONE waiting to be delivered

static void stop(const char *why);

static int32_t tint(const Tuple *t) {
  if (!t) return 0;
  if (t->length == 1) return t->type == TUPLE_INT ? t->value->int8 : t->value->uint8;
  if (t->length == 2) return t->type == TUPLE_INT ? t->value->int16 : t->value->uint16;
  return t->value->int32;
}

static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void quiet_fired(void *data) { s_quiet = NULL; stop(s_up || s_stage ? "phone went quiet" : NULL); }

static void alive(void) {
  if (s_quiet) app_timer_reschedule(s_quiet, QUIET_MS);
  else s_quiet = app_timer_register(QUIET_MS, quiet_fired, NULL);
}

static void free_stage(void) { free(s_stage); s_stage = NULL; }

static void stop(const char *why) {
  if (why) APP_LOG(APP_LOG_LEVEL_INFO, "sync: %s", why);
  free(s_up);
  s_up = NULL;
  free_stage();
  if (s_quiet) { app_timer_cancel(s_quiet); s_quiet = NULL; }
}

// ---- Upload ----

static int pack_log(Upload *u) {
  while (u->log_i < u->nlogs) {
    int len = store_log_chunk(u->log_i++, u->rec, sizeof(u->rec));
    if (len > 0) return len;
  }
  return 0;
}

static int pack_hist(Upload *u) {
  while (u->hist_i < u->nhist) {
    int len = store_hist_record(u->hist_i++, u->rec, sizeof(u->rec));
    if (len > 0) return len;
  }
  return 0;
}

static int pack_note(Upload *u) {
  while (u->note_i < u->nnotes) {
    if (u->part == 0) {
      if (!notes_read(u->ids[u->note_i], &u->note, u->text)) { u->note_i++; continue; }  // gone meanwhile
      u->parts = cc_note_parts(u->note.text_len);
      u->part = 1;
      u->rec_type = CC_T_NOTE_HEAD;
      return cc_pack_note_head(&u->note, u->text, u->rec, sizeof(u->rec));
    }
    if (u->part < u->parts) {
      u->rec_type = CC_T_NOTE_PART;
      return cc_pack_note_part(&u->note, u->text, u->part++, u->rec, sizeof(u->rec));
    }
    u->note_i++;
    u->part = 0;
  }
  return 0;
}

// The next record to upload into u->rec (type in u->rec_type), or 0 when there is none left.
static int next_record(Upload *u) {
  int len;
  while (u->phase != P_END) {
    switch (u->phase) {
      case P_SCHED: {
        if (!u->full_sched || u->item >= g_nitems) { u->phase = P_SETTINGS; break; }
        const CcItem *it = &g_items[u->item];
        if (u->step < 0) {
          u->step = 0;
          u->rec_type = CC_T_ITEM;
          return cc_pack_item(it, u->rec, sizeof(u->rec));
        }
        if (u->step < it->step_count + it->alt_count) {
          int j = u->step++;
          u->rec_type = CC_T_STEP;
          return cc_pack_step_at(it->id, (uint8_t)j, &coach_steps(u->item)[j], u->rec, sizeof(u->rec));
        }
        u->item++;
        u->step = -1;
        break;
      }
      case P_SETTINGS:
        u->phase = P_LOGS;
        u->rec_type = CC_T_SETTINGS;
        return cc_pack_settings_at(&g_cfg, s_sync.settings_utc, u->rec, sizeof(u->rec));
      case P_LOGS:
        if ((len = pack_log(u)) > 0) { u->rec_type = CC_T_LOG; return len; }
        u->phase = P_HIST;
        break;
      case P_HIST:
        if ((len = pack_hist(u)) > 0) { u->rec_type = CC_T_HIST; return len; }
        u->phase = P_NOTES;
        break;
      case P_NOTES:
        if ((len = pack_note(u)) != 0) return len;
        u->phase = P_TOMBS;
        break;
      case P_TOMBS: {
        u->phase = P_END;
        uint32_t t[NOTES_TOMB_MAX];
        int n = notes_tombs(t, NOTES_TOMB_MAX);
        if (n > 0) { u->rec_type = CC_T_TOMBS; return cc_pack_tombs(t, n, u->rec, sizeof(u->rec)); }
        break;
      }
      default: u->phase = P_END; break;
    }
  }
  return 0;
}

static bool send_message(int32_t cmd, const uint8_t *data, int len, bool with_rev, uint32_t rev, int32_t arg) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return false;
  dict_write_int32(it, MESSAGE_KEY_CMD, cmd);
  if (data && len) dict_write_data(it, MESSAGE_KEY_DATA, data, (uint16_t)len);
  if (with_rev) dict_write_uint32(it, MESSAGE_KEY_REV, rev);
  dict_write_int32(it, MESSAGE_KEY_ARG, arg);
  if (app_message_outbox_send() != APP_MSG_OK) return false;
  s_sending = true;
  s_inflight = cmd;
  return true;
}

// Fills the next UP message, or sends UP_END when everything is out.
static void fill(Upload *u) {
  u->out_len = 0;
  for (;;) {
    if (!u->rec_type) {
      u->rec_len = next_record(u);
      if (u->rec_len < 0) { APP_LOG(APP_LOG_LEVEL_WARNING, "sync: record %d failed to pack", u->rec_type); u->rec_type = 0; continue; }
      if (u->rec_len == 0) { u->rec_type = 0; break; }
    }
    int pos = cc_frame_put(u->out, DATA_MAX, u->out_len, u->rec_type, u->rec, u->rec_len);
    if (pos < 0) break;  // full: this record opens the next message
    u->out_len = pos;
    u->rec_type = 0;
  }
}

static void retry_fired(void *data);

static void send_up(void) {
  Upload *u = s_up;
  if (!u || s_sending) return;
  bool ok;
  if (u->out_len) ok = send_message(C_UP, u->out, u->out_len, false, 0, 0);
  else ok = send_message(C_UP_END, NULL, 0, true, s_sync.rev, u->nnotes);
  if (!ok && !s_retry) { s_retry = app_timer_register(RETRY_MS, retry_fired, NULL); return; }
  if (ok) alive();
}

static void send_reply(void) {
  if (s_reply == INT32_MIN || s_sending) return;
  if (!send_message(C_DONE, NULL, 0, false, 0, s_reply) && !s_retry)
    s_retry = app_timer_register(RETRY_MS, retry_fired, NULL);
}

static void retry_fired(void *data) {
  s_retry = NULL;
  if (s_up) send_up();
  send_reply();
}

static void sent(DictionaryIterator *it, void *ctx) {
  s_sending = false;
  s_fails = 0;
  if (s_inflight == C_DONE) s_reply = INT32_MIN;
  if (s_up && s_inflight == C_UP) {
    s_up->msgs++;
    fill(s_up);
  } else if (s_up && s_inflight == C_UP_END) {
    APP_LOG(APP_LOG_LEVEL_INFO, "sync: up %d msgs %d notes", s_up->msgs, s_up->nnotes);
    free(s_up);
    s_up = NULL;
  }
  send_reply();
  send_up();
}

// One retry of the same message, then the phone is gone: nothing is marked synced without an ACK.
static void failed(DictionaryIterator *it, AppMessageResult reason, void *ctx) {
  s_sending = false;
  APP_LOG(APP_LOG_LEVEL_WARNING, "sync: send %d failed %d", (int)s_inflight, (int)reason);
  if (++s_fails > 1) {
    s_fails = 0;
    s_reply = INT32_MIN;
    stop("phone not answering");
    return;
  }
  if (!s_retry) s_retry = app_timer_register(RETRY_MS, retry_fired, NULL);
}

// The index of the oldest log chunk the phone may lack: chunks are in time order, so scan back
// from the newest until one ends before the phone's latest entry.
static int first_log(uint32_t log_utc, uint8_t *buf, int cap) {
  int n = store_log_chunks();
  if (log_utc == 0) return 0;
  CcLogEntry e[CC_LOG_PER_CHUNK];
  for (int i = n - 1; i >= 0; i--) {
    int len = store_log_chunk(i, buf, cap);
    int k = len > 0 ? cc_unpack_logs(buf, len, e, CC_LOG_PER_CHUNK) : -1;
    if (k > 0 && (uint32_t)e[k - 1].utc < log_utc) return i + 1;
  }
  return 0;
}

static int first_day(int32_t hist_day, uint8_t *buf, int cap) {
  int n = store_hist_days();
  CcOutcome o[CC_MAX_ITEMS];
  for (int i = n - 1; i >= 0; i--) {
    int32_t day;
    int len = store_hist_record(i, buf, cap);
    if (len > 0 && cc_unpack_history(buf, len, &day, o, CC_MAX_ITEMS) >= 0 && day <= hist_day) return i + 1;
  }
  return 0;
}

static void on_hello(const Tuple *data) {
  if (!data || data->length < 16) return;
  const uint8_t *d = data->value->data;
  uint32_t phone = get32(d), rev = get32(d + 4), log_utc = get32(d + 8);
  int32_t hist_day = (int32_t)get32(d + 12);
  stop(s_up ? "hello again: starting over" : NULL);
  Upload *u = calloc(1, sizeof(Upload));
  if (!u) { APP_LOG(APP_LOG_LEVEL_ERROR, "sync: no memory"); return; }
  s_up = u;
  s_hello_phone = phone;
  u->full_sched = rev == 0;
  u->phase = u->full_sched ? P_SCHED : P_SETTINGS;
  u->step = -1;
  u->nlogs = store_log_chunks();
  u->log_i = first_log(log_utc, u->rec, sizeof(u->rec));
  u->nhist = store_hist_days();
  u->hist_i = first_day(hist_day, u->rec, sizeof(u->rec));
  u->nnotes = notes_unsynced(u->ids, NOTES_SLOTS, phone != s_sync.phone_id);
  APP_LOG(APP_LOG_LEVEL_INFO, "sync: hello rev %u, logs from %d/%d, days from %d/%d, %d notes", (unsigned)rev,
          u->log_i, u->nlogs, u->hist_i, u->nhist, u->nnotes);
  fill(u);
  send_up();
}

static void on_ack(const Tuple *data, const Tuple *rev, const Tuple *arg) {
  int n = data ? data->length / 4 : 0;
  for (int i = 0; i < n; i++) notes_mark_synced(get32(data->value->data + 4 * i));
  notes_drop_tombs(tint(arg));
  if (tint(rev)) s_sync.rev = (uint32_t)tint(rev);
  s_sync.phone_id = s_hello_phone;
  s_sync.last_sync_utc = (uint32_t)time(NULL);
  store_save_sync(&s_sync);
  APP_LOG(APP_LOG_LEVEL_INFO, "sync: ack %d notes, rev %u", n, (unsigned)s_sync.rev);
}

// ---- Schedule from the phone ----

static int staged_index(uint16_t id) {
  for (int i = 0; i < s_stage->n; i++)
    if (s_stage->items[i].id == id) return i;
  return -1;
}

static void on_sched(const Tuple *data, bool first) {
  if (first) {
    free_stage();
    s_stage = calloc(1, sizeof(Stage));
    if (!s_stage) { APP_LOG(APP_LOG_LEVEL_ERROR, "sync: no memory for the schedule"); return; }
  }
  Stage *st = s_stage;
  if (!st || !data) return;
  const uint8_t *buf = data->value->data, *rec;
  int len = data->length, pos = 0, rlen;
  uint8_t type;
  while ((pos = cc_frame_next(buf, len, pos, &type, &rec, &rlen)) > 0) {
    if (type == CC_T_ITEM) {
      CcItem *it = &st->items[st->n];
      if (st->n >= CC_MAX_ITEMS || !cc_unpack_item(rec, rlen, it) ||
          st->used + it->step_count + it->alt_count > CC_POOL_MAX) {
        st->bad = true;
      } else {
        it->step_first = (uint8_t)st->used;
        st->used += it->step_count + it->alt_count;
        st->n++;
      }
    } else if (type == CC_T_STEP) {
      uint16_t item; uint8_t index; CcStep s;
      int k = cc_unpack_step_at(rec, rlen, &item, &index, &s) ? staged_index(item) : -1;
      if (k < 0 || index >= st->items[k].step_count + st->items[k].alt_count) st->bad = true;
      else { st->pool[st->items[k].step_first + index] = s; st->mask[k] |= 1ull << index; }
    } else if (type == CC_T_SETTINGS) {
      st->have_cfg = cc_unpack_settings_at(rec, rlen, &st->cfg, &st->cfg_utc);
      if (!st->have_cfg) st->bad = true;
    }
  }
  if (pos < 0) st->bad = true;
}

// Only under home or Today: alerts and the runner hold pointers into the schedule.
static bool can_commit(void) {
  return !window_stack_get_top_window() || home_win_on_top() || today_win_on_top();
}

static void commit(Stage *st, uint32_t rev) {
  int old = g_run.active ? coach_slot(g_run.item_id) : -1;
  CcStep was;  // the run's current step before the save: an edit to it wins over a changed count
  if (old >= 0) was = *cc_run_cur(&g_run, coach_steps(old));
  memcpy(g_items, st->items, sizeof(CcItem) * st->n);
  memcpy(g_pool, st->pool, sizeof(CcStep) * st->used);
  g_nitems = st->n;
  cc_pool_layout(g_items, g_nitems);
  if (st->have_cfg) {
    g_cfg = st->cfg;
    s_sync.settings_utc = st->cfg_utc;
    store_save_settings();
  }
  store_save_schedule();
  cc_sec now = clock_now_local();
  int slot = g_run.active ? coach_slot(g_run.item_id) : -1;
  if (g_run.active) {  // the edits win: the run takes its routine's new steps, or ends if they changed shape
    if (slot < 0 || !cc_run_refit(&g_run, &g_items[slot], coach_steps(slot), old >= 0 ? &was : NULL, now)) {
      g_run.active = 0;
      g_run.step_end = CC_NEVER;
    }
    store_save_run();
  }
  if (g_focus.label_id && !coach_item(g_focus.label_id)) {
    g_focus.label_id = 0;
    store_save_focus();
  }
  coach_ensure_today(now);
  cc_day_prune(g_day, g_items, g_nitems);
  cc_sweep(g_day, g_items, g_nitems, now, &g_cfg);
  store_save_day();
  s_sync.rev = rev;
  store_save_sync(&s_sync);
  ui_refresh_all();
  wake_rearm();
  glance_update();
}

static void reply(int32_t arg) {
  s_reply = arg;
  send_reply();
}

// Every record came, and every item's alternatives each belong to one main step.
static bool stage_ok(const Stage *st, int count) {
  if (st->bad || st->n != count || !cc_stage_complete(st->items, st->n, st->mask)) return false;
  for (int i = 0; i < st->n; i++)
    if (!cc_item_steps_ok(&st->items[i], &st->pool[st->items[i].step_first])) return false;
  return true;
}

static void on_sched_end(uint32_t rev, int count) {
  Stage *st = s_stage;
  const char *how;
  int32_t arg;
  if (!st || !stage_ok(st, count)) { how = "invalid"; arg = -1; }
  else if (!can_commit()) { how = "busy"; arg = 0; }
  else { commit(st, rev); how = "applied"; arg = 1; }
  APP_LOG(APP_LOG_LEVEL_INFO, "sync: schedule rev %u %d items %s", (unsigned)rev, st ? st->n : 0, how);
  free_stage();
  reply(arg);
}

static void on_ops(const Tuple *data) {
  int n = 0;
  if (data) {
    const uint8_t *buf = data->value->data, *rec;
    int pos = 0, rlen;
    uint8_t type;
    while ((pos = cc_frame_next(buf, data->length, pos, &type, &rec, &rlen)) > 0) {
      uint32_t id;
      char name[CC_NAME_MAX];
      if (type == CC_T_RENAME && cc_unpack_rename(rec, rlen, &id, name)) n += notes_apply_rename(id, name);
      else if (type == CC_T_DELETE && rlen >= 4) n += notes_apply_delete(get32(rec));
    }
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "sync: ops %d", n);
  reply(1);
}

static void received(DictionaryIterator *it, void *ctx) {
  Tuple *cmd = dict_find(it, MESSAGE_KEY_CMD);
  if (!cmd) return;
  Tuple *data = dict_find(it, MESSAGE_KEY_DATA), *rev = dict_find(it, MESSAGE_KEY_REV), *arg = dict_find(it, MESSAGE_KEY_ARG);
  if (data && data->type != TUPLE_BYTE_ARRAY) data = NULL;
  alive();
  switch (tint(cmd)) {
    case C_HELLO: on_hello(data); break;
    case C_ACK: on_ack(data, rev, arg); break;
    case C_SCHED: on_sched(data, tint(arg) == 1); break;
    case C_SCHED_END: on_sched_end((uint32_t)tint(rev), tint(arg)); break;
    case C_OPS: on_ops(data); break;
    default: break;
  }
}

static void dropped(AppMessageResult reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "sync: message dropped %d", (int)reason);
  if (s_stage) s_stage->bad = true;  // a lost SCHED message: refuse the rest
}

void sync_init(void) {
  store_load_sync(&s_sync);
  app_message_register_inbox_received(received);
  app_message_register_inbox_dropped(dropped);
  app_message_register_outbox_sent(sent);
  app_message_register_outbox_failed(failed);
  app_message_open(1024, 1024);
}

void sync_settings_changed(void) {
  s_sync.settings_utc = (uint32_t)time(NULL);
  store_save_sync(&s_sync);
}

uint32_t sync_last_utc(void) { return s_sync.last_sync_utc; }
