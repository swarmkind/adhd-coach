// Prints byte vectors as JSON so the phone's codec.js can be checked against the C records.
// Built and run by `make -C test js`.
#include <stdio.h>
#include <string.h>
#include "cc_codec.h"
#include "cc_note.h"
#include "cc_sync.h"
#include "cc_event.h"
#include "cc_time.h"

static int s_first;

static void hex(const uint8_t *b, int n) {
  putchar('"');
  for (int i = 0; i < n; i++) printf("%02x", b[i]);
  putchar('"');
}

static void jstr(const char *s) {
  putchar('"');
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') putchar('\\');
    putchar(*s);
  }
  putchar('"');
}

static void sep(void) { if (!s_first) printf(",\n"); s_first = 0; }

static void num_json(const CcNumSpec *n) {
  printf("{\"step\":%d,\"min\":%d,\"max\":%d,\"decimals\":%d,\"unit\":", n->step, (int)n->min, (int)n->max, n->decimals);
  jstr(n->unit);
  putchar('}');
}

static void item_vector(const CcItem *it) {
  uint8_t buf[256];
  int n = cc_pack_item(it, buf, sizeof(buf));
  sep();
  printf("{\"json\":{\"id\":%d,\"kind\":%d,\"priority\":%d,\"flags\":%d,\"days\":%d,\"at\":%d,\"until\":%d,"
         "\"every\":%d,\"goal\":%d,\"grace\":%d,\"order\":%d,\"stepCount\":%d,\"eventDay\":%d,\"ladder\":[",
         it->id, it->kind, it->priority, it->flags, it->days, it->at_min, it->until_min, it->every_min, it->goal,
         it->grace_min, it->order, it->step_count, (int)it->event_day);
  for (int i = 0; i < it->ladder_count; i++) printf("%s%d", i ? "," : "", it->ladder[i]);
  printf("],\"num\":");
  num_json(&it->num);
  printf(",\"name\":");
  jstr(it->name);
  printf("},\"hex\":");
  hex(buf, n);
  putchar('}');
}

static void step_vector(uint16_t item, uint8_t index, const CcStep *s) {
  uint8_t buf[256];
  int n = cc_pack_step_at(item, index, s, buf, sizeof(buf));
  sep();
  printf("{\"item\":%d,\"index\":%d,\"json\":{\"type\":%d,\"dur\":%d,\"num\":", item, index, s->type, s->dur_s);
  num_json(&s->num);
  printf(",\"name\":");
  jstr(s->name);
  printf("},\"hex\":");
  hex(buf, n);
  putchar('}');
}

static CcItem item(uint16_t id, uint8_t kind, const char *name) {
  CcItem it; memset(&it, 0, sizeof(it));
  it.id = id; it.kind = kind;
  strncpy(it.name, name, CC_NAME_MAX - 1);
  cc_utf8_trunc(it.name, CC_NAME_MAX);
  return it;
}

static CcStep step(uint8_t type, const char *name, uint16_t dur) {
  CcStep s; memset(&s, 0, sizeof(s));
  s.type = type; s.dur_s = dur;
  strncpy(s.name, name, CC_NAME_MAX - 1);
  return s;
}

int main(void) {
  uint8_t buf[256];
  int n;
  printf("{\"items\":[\n");
  s_first = 1;
  CcItem r = item(1, CC_ROUTINE, "Morning routine");
  r.at_min = 420; r.days = 0x3E; r.grace_min = 15; r.step_count = 4;
  item_vector(&r);
  CcItem p = item(3, CC_REMINDER, "10 pushups");
  p.at_min = 600; p.until_min = 1200; p.every_min = 120; p.days = 0x7F; p.flags = CC_F_ASK_NUMBER; p.goal = 4;
  p.priority = CC_IMPORTANT;
  p.num = (CcNumSpec){ 1, 0, 500, 0, "reps" };
  item_vector(&p);
  CcItem e = item(6, CC_EVENT, "Caf\xC3\xA9 with Zo\xC3\xAB \xF0\x9F\x8E\x82 and the long name");  // cut at 31 bytes
  e.at_min = 630; e.event_day = 20406;
  e.ladder_count = (uint8_t)cc_ladder_preset(CC_LADDER_PERSISTENT, e.ladder);
  e.flags = CC_F_SILENT;
  item_vector(&e);
  CcItem t = item(5, CC_TASK, "Tidy desk");
  t.flags = CC_F_ANYTIME; t.days = 0x7F; t.order = 4; t.priority = CC_CRITICAL;
  item_vector(&t);
  CcItem w = item(200, CC_REMINDER, "Weigh-in");
  w.at_min = 0; w.until_min = 1439; w.every_min = 1439; w.days = 0x41; w.flags = CC_F_ASK_NUMBER;
  w.num = (CcNumSpec){ -5, -1000, 2147483647, 2, "\xC2\xB0" "C" };
  item_vector(&w);

  printf("\n],\"steps\":[\n");
  s_first = 1;
  CcStep s0 = step(CC_STEP_MANUAL, "Drink a glass of water", 0);
  CcStep s1 = step(CC_STEP_TIMED, "Plank", 60);
  CcStep s2 = step(CC_STEP_NUMBER, "Weight", 0);
  s2.num = (CcNumSpec){ 1, 300, 2500, 1, "kg" };
  CcStep s3 = step(CC_STEP_TIMED, "Stretch hamstrings and hips", 3600);
  step_vector(1, 0, &s0); step_vector(1, 1, &s1); step_vector(1, 2, &s2); step_vector(1, 3, &s3);

  CcSettings c = CC_SETTINGS_DEFAULT;
  c.focus_work_min = 30; c.event_time_min = 600; c.later_event_min = 240;
  n = cc_pack_settings_at(&c, 1761200000u, buf, sizeof(buf));
  printf("\n],\"settings\":{\"utc\":1761200000,\"json\":{\"laterTask\":%d,\"laterReminder\":%d,\"headsupImportant\":%d,"
         "\"headsupCritical\":%d,\"catchup\":%d,\"eventTime\":%d,\"laterEvent\":%d,\"focusWork\":%d,\"focusShort\":%d,"
         "\"focusLong\":%d,\"focusCycles\":%d},\"hex\":",
         c.later_task_min, c.later_reminder_min, c.headsup_important_min, c.headsup_critical_min, c.catchup_min,
         c.event_time_min, c.later_event_min, c.focus_work_min, c.focus_short_min, c.focus_long_min, c.focus_cycles);
  hex(buf, n);

  CcLogEntry logs[3] = { { 1761200000, 1, 2, 824 }, { 1761200000, 3, 255, 12 }, { 1761203600, 200, 255, -40 } };
  n = cc_pack_logs(logs, 3, buf, sizeof(buf));
  printf("},\n\"logs\":{\"json\":[");
  for (int i = 0; i < 3; i++)
    printf("%s{\"t\":%d,\"item\":%d,\"step\":%d,\"v\":%d}", i ? "," : "", (int)logs[i].utc, logs[i].item, logs[i].step,
           (int)logs[i].value);
  printf("],\"hex\":");
  hex(buf, n);

  CcOutcome out[2] = { { 1, CC_ST_DONE, 0, 0 }, { 2, CC_ST_NONE, 6, 2 } };
  n = cc_pack_history(20384, out, 2, buf, sizeof(buf));
  printf("},\n\"history\":{\"json\":{\"day\":20384,\"out\":[{\"id\":1,\"status\":%d,\"count\":0,\"missed\":0},"
         "{\"id\":2,\"status\":%d,\"count\":6,\"missed\":2}]},\"hex\":", CC_ST_DONE, CC_ST_NONE);
  hex(buf, n);

  // A full-length note whose multi-byte characters straddle the record boundaries.
  static char text[1200];
  text[0] = 0;
  while (strlen(text) < 1000) strcat(text, "Caf\xC3\xA9 \xF0\x9F\x8C\xB1 idea, ");
  CcNote note; memset(&note, 0, sizeof(note));
  note.id = 1792800000u; note.len_s = 12; note.link = 1; note.status = CC_NOTE_OK; note.reason = 0;
  note.flags = CC_NOTE_F_SYNCED;
  strcpy(note.name, "Garden \xF0\x9F\x8C\xB1");
  note.text_len = cc_note_clip(text);
  printf("},\n\"note\":{\"json\":{\"id\":%u,\"len\":12,\"link\":1,\"status\":0,\"reason\":0,\"flags\":1,\"name\":",
         (unsigned)note.id);
  jstr(note.name);
  printf(",\"text\":");
  jstr(text);
  printf("},\"records\":[");
  n = cc_pack_note_head(&note, text, buf, sizeof(buf));
  hex(buf, n);
  for (int part = 1; part < cc_note_parts(note.text_len); part++) {
    n = cc_pack_note_part(&note, text, part, buf, sizeof(buf));
    putchar(',');
    hex(buf, n);
  }
  CcNote missed; memset(&missed, 0, sizeof(missed));
  missed.id = 1792800100u; missed.len_s = 3; missed.status = CC_NOTE_MISSED; missed.reason = 5;
  n = cc_pack_note_head(&missed, "", buf, sizeof(buf));
  printf("]},\n\"missed\":{\"json\":{\"id\":%u,\"len\":3,\"link\":0,\"status\":1,\"reason\":5,\"flags\":0,\"name\":\"\","
         "\"text\":\"\"},\"hex\":", (unsigned)missed.id);
  hex(buf, n);

  uint32_t ids[3] = { 1792800000u, 1792800001u, 4000000000u };
  n = cc_pack_tombs(ids, 3, buf, sizeof(buf));
  printf("},\n\"tombs\":{\"json\":[1792800000,1792800001,4000000000],\"hex\":");
  hex(buf, n);
  n = cc_pack_rename(1792800000u, "Garden id\xC3\xA9" "as \xF0\x9F\x8C\xB1 for the back of the house", buf, sizeof(buf));
  printf("},\n\"rename\":{\"id\":1792800000,\"name\":");
  jstr("Garden id\xC3\xA9" "as \xF0\x9F\x8C\xB1 for the back of the house");
  printf(",\"hex\":");
  hex(buf, n);

  uint8_t frames[64];
  const uint8_t a[] = { 1, 2, 3 }, b[] = { 0xFF };
  int fp = cc_frame_put(frames, sizeof(frames), 0, CC_T_ITEM, a, 3);
  fp = cc_frame_put(frames, sizeof(frames), fp, CC_T_DELETE, b, 1);
  printf("},\n\"frames\":{\"json\":[{\"type\":%d,\"rec\":\"010203\"},{\"type\":%d,\"rec\":\"ff\"}],\"hex\":", CC_T_ITEM,
         CC_T_DELETE);
  hex(frames, fp);

  int y, m, d, day = 20000;
  do { cc_date_of(++day, &y, &m, &d); } while (!(y == 2026 && m == 11 && d == 14));
  printf("},\n\"days\":[{\"iso\":\"2026-11-14\",\"day\":%d},", day);
  cc_date_of(0, &y, &m, &d);
  printf("{\"iso\":\"%04d-%02d-%02d\",\"day\":0},", y, m, d);
  day = 20000;
  do { cc_date_of(++day, &y, &m, &d); } while (!(y == 2028 && m == 2 && d == 29));
  printf("{\"iso\":\"2028-02-29\",\"day\":%d}]}\n", day);
  return 0;
}
