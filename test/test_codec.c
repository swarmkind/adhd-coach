#include "t.h"
#include "cc_codec.h"
#include "cc_time.h"

static void fill_item(CcItem *it) {
  memset(it, 0, sizeof(*it));
  it->id = 7; it->kind = CC_REMINDER; it->priority = CC_IMPORTANT;
  it->flags = CC_F_ASK_NUMBER | CC_F_SILENT; it->days = 0x7F;
  it->at_min = 540; it->until_min = 1260; it->every_min = 60;
  it->goal = 8; it->grace_min = 15; it->order = 3; it->step_count = 2;
  it->event_day = 20749; it->ladder_count = 3;
  it->ladder[0] = 7; it->ladder[1] = 1; it->ladder[2] = -1;
  it->num.step = 1; it->num.min = 300; it->num.max = 2500; it->num.decimals = 1;
  strcpy(it->num.unit, "kg");
  strcpy(it->name, "Drink water");
}

void test_codec(void) {
  uint8_t buf[256];
  int n;

  // item round trip
  CcItem a, b;
  fill_item(&a);
  n = cc_pack_item(&a, buf, sizeof(buf));
  T_TRUE(n > 0 && n <= 256);
  memset(&b, 0xAB, sizeof(b));
  T_TRUE(cc_unpack_item(buf, n, &b));
  T_TRUE(memcmp(&a, &b, sizeof(a)) == 0);

  // step round trip
  CcStep s, s2;
  memset(&s, 0, sizeof(s));
  s.type = CC_STEP_NUMBER; s.amount.start = 90; s.alt = CC_NO_ALT; s.num.step = 5; s.num.min = -100; s.num.max = 99999;
  s.num.decimals = 2; strcpy(s.num.unit, "reps"); strcpy(s.name, "Stretch hamstrings and hips");
  n = cc_pack_step(&s, buf, sizeof(buf));
  T_TRUE(n > 0 && n <= 256);
  T_TRUE(cc_unpack_step(buf, n, &s2));
  T_TRUE(memcmp(&s, &s2, sizeof(s)) == 0);

  // V2 items carry alternatives and a rhythm; a V1 item reads with neither
  CcItem iv2, ivb;
  fill_item(&iv2);
  iv2.kind = CC_ROUTINE; iv2.step_count = 3; iv2.alt_count = 1; iv2.every_days = 3; iv2.every_since = 20749;
  n = cc_pack_item(&iv2, buf, sizeof(buf));
  T_TRUE(n > 0 && buf[0] == CC_CODEC_V2);
  memset(&ivb, 0xAB, sizeof(ivb));
  T_TRUE(cc_unpack_item(buf, n, &ivb));
  ivb.step_first = iv2.step_first;  // never stored
  T_TRUE(memcmp(&iv2, &ivb, sizeof(iv2)) == 0);
  iv2.step_count = 21; n = cc_pack_item(&iv2, buf, sizeof(buf)); T_TRUE(!cc_unpack_item(buf, n, &ivb));
  iv2.step_count = 3; iv2.alt_count = 4; n = cc_pack_item(&iv2, buf, sizeof(buf)); T_TRUE(!cc_unpack_item(buf, n, &ivb));  // more alternatives than steps
  iv2.alt_count = 1; iv2.every_days = 31; n = cc_pack_item(&iv2, buf, sizeof(buf)); T_TRUE(!cc_unpack_item(buf, n, &ivb));
  // 1.4.1's record for "Tidy desk" (anytime task): version 1, no new fields
  const uint8_t item_v1[] = { 1, 5, 0, CC_TASK, CC_CRITICAL, CC_F_ANYTIME, 0x7F, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0,
                              0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                              9, 'T', 'i', 'd', 'y', ' ', 'd', 'e', 's', 'k' };
  memset(&ivb, 0xAB, sizeof(ivb));
  T_TRUE(cc_unpack_item(item_v1, sizeof(item_v1), &ivb));
  T_EQ(ivb.id, 5); T_EQ(ivb.order, 4); T_STR(ivb.name, "Tidy desk");
  T_EQ(ivb.alt_count, 0); T_EQ(ivb.every_days, 0); T_EQ(ivb.every_since, 0);

  // V2 steps: every field; a V1 step's length becomes its amount
  CcStep f, f2;
  memset(&f, 0, sizeof(f));
  f.type = CC_STEP_MANUAL; f.flags = CC_SF_QTY | CC_SF_WEIGHT; f.alt = 5; f.show_every = 2; f.show_since = 20750;
  f.amount = (CcRamp){ 20, 40, 20749, 1, 7 }; f.weight = (CcRamp){ 125, 0, 20749, 25, 28 };
  f.num = (CcNumSpec){ 0, 0, 0, 1, "kg" };
  strncpy(f.name, "Curls with a long name for the cut", CC_NAME_MAX - 1);
  cc_utf8_trunc(f.name, CC_NAME_MAX);
  n = cc_pack_step(&f, buf, sizeof(buf));
  T_TRUE(n > 0 && n + 3 <= 100 && buf[0] == CC_CODEC_V2);
  T_TRUE(cc_unpack_step(buf, n, &f2));
  T_TRUE(memcmp(&f, &f2, sizeof(f)) == 0);
  const uint8_t step_v1[] = { 1, CC_STEP_TIMED, 90, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  5, 'P', 'l', 'a', 'n', 'k' };
  T_TRUE(cc_unpack_step(step_v1, sizeof(step_v1), &f2));
  T_EQ(f2.type, CC_STEP_TIMED); T_EQ(f2.amount.start, 90); T_EQ(f2.amount.inc, 0); T_EQ(f2.alt, CC_NO_ALT);
  T_EQ(f2.flags, 0); T_EQ(f2.show_every, 0); T_STR(f2.name, "Plank");
  f.show_every = 31; n = cc_pack_step(&f, buf, sizeof(buf)); T_TRUE(!cc_unpack_step(buf, n, &f2));
  f.show_every = 2; f.amount.every = 0; n = cc_pack_step(&f, buf, sizeof(buf)); T_TRUE(!cc_unpack_step(buf, n, &f2));
  f.amount.every = 7; f.type = 3; n = cc_pack_step(&f, buf, sizeof(buf)); T_TRUE(!cc_unpack_step(buf, n, &f2));

  // settings round trip
  CcSettings c = CC_SETTINGS_DEFAULT, c2;
  memset(&c2, 0, sizeof(c2));
  n = cc_pack_settings(&c, buf, sizeof(buf));
  T_TRUE(n > 0 && cc_unpack_settings(buf, n, &c2));
  T_EQ(c2.later_task_min, 30); T_EQ(c2.later_reminder_min, 10);
  T_EQ(c2.headsup_important_min, 10); T_EQ(c2.headsup_critical_min, 15); T_EQ(c2.catchup_min, 10);

  // settings v2 round trip, every field off its default
  CcSettings v = { 45, 20, 5, 30, 25, 600, 90, 50, 10, 20, 3 }, v2;
  memset(&v2, 0, sizeof(v2));
  n = cc_pack_settings(&v, buf, sizeof(buf));
  T_TRUE(n > 0 && buf[0] == CC_CODEC_V2);
  T_TRUE(cc_unpack_settings(buf, n, &v2));
  T_TRUE(memcmp(&v, &v2, sizeof(v)) == 0);

  // a V1 settings record keeps its five fields; the new ones default
  const uint8_t v1[] = { 1, 20, 5, 10, 15, 10 };
  memset(&v2, 0, sizeof(v2));
  T_TRUE(cc_unpack_settings(v1, sizeof(v1), &v2));
  T_EQ(v2.later_task_min, 20); T_EQ(v2.later_reminder_min, 5);
  T_EQ(v2.headsup_important_min, 10); T_EQ(v2.headsup_critical_min, 15); T_EQ(v2.catchup_min, 10);
  T_EQ(v2.event_time_min, 540); T_EQ(v2.later_event_min, 120);
  T_EQ(v2.focus_work_min, 25); T_EQ(v2.focus_short_min, 5);
  T_EQ(v2.focus_long_min, 15); T_EQ(v2.focus_cycles, 4);

  // an unknown future version is refused
  const uint8_t v3[] = { 3, 20, 5, 10, 15, 10, 28, 2, 120, 25, 5, 15, 4 };
  T_TRUE(!cc_unpack_settings(v3, sizeof(v3), &v2));

  // a full 40-item day in 3 chunks
  static CcDay d, d2;
  memset(&d, 0, sizeof(d)); memset(&d2, 0, sizeof(d2));
  d.day = 20749; d.n = CC_MAX_ITEMS;
  for (int i = 0; i < CC_MAX_ITEMS; i++) {
    d.s[i].id = (uint16_t)(100 + i); d.s[i].status = (uint8_t)(i % 6); d.s[i].count = (uint8_t)i;
    d.s[i].missed = 2; d.s[i].auto_later_used = 1; d.s[i].flags = CC_SF_HEADSUP_SHOWN;
    d.s[i].later_until = 1792738800 + i; d.s[i].handled_until = 1792700000 - i;
  }
  for (int ch = 0; ch < 3; ch++) {
    n = cc_pack_day_chunk(&d, ch, buf, sizeof(buf));
    T_TRUE(n > 0 && n <= 256);
    T_TRUE(cc_unpack_day_chunk(buf, n, &d2));
  }
  T_TRUE(memcmp(&d, &d2, sizeof(d)) == 0);
  T_EQ(cc_pack_day_chunk(&d, 3, buf, sizeof(buf)), -1);  // no 4th chunk for 40 items

  // 20 log entries in one chunk
  CcLogEntry e[CC_LOG_PER_CHUNK], e2[CC_LOG_PER_CHUNK];
  memset(e, 0, sizeof(e)); memset(e2, 0, sizeof(e2));
  for (int i = 0; i < CC_LOG_PER_CHUNK; i++) {
    e[i].utc = 1792738800 + i * 60; e[i].item = (uint16_t)i; e[i].step = 3; e[i].value = 824 - i;
  }
  n = cc_pack_logs(e, CC_LOG_PER_CHUNK, buf, sizeof(buf));
  T_TRUE(n > 0 && n <= 256);
  T_EQ(cc_unpack_logs(buf, n, e2, CC_LOG_PER_CHUNK), CC_LOG_PER_CHUNK);
  T_TRUE(memcmp(e, e2, sizeof(e)) == 0);

  // UTF-8 truncation never splits a character
  char u[64] = "ééééééééééééééééé";  // 17 x 2 bytes
  cc_utf8_trunc(u, 32);
  T_EQ((int)strlen(u), 30);
  char ascii[64] = "abcdefghijklmnopqrstuvwxyz0123456789ABCD";  // 40 bytes
  cc_utf8_trunc(ascii, CC_NAME_MAX);
  T_EQ((int)strlen(ascii), 31);

  // rejects
  fill_item(&a);
  n = cc_pack_item(&a, buf, sizeof(buf));
  buf[0] = 3;
  T_TRUE(!cc_unpack_item(buf, n, &b));               // wrong version
  buf[0] = CC_CODEC_V2;
  T_TRUE(!cc_unpack_item(buf, 10, &b));              // short buffer
  a.until_min = 500;                                 // reminder window ends before it starts
  n = cc_pack_item(&a, buf, sizeof(buf));
  T_TRUE(!cc_unpack_item(buf, n, &b));
  fill_item(&a); a.step_count = CC_MAX_STEPS + 1;
  n = cc_pack_item(&a, buf, sizeof(buf));
  T_TRUE(!cc_unpack_item(buf, n, &b));
  T_EQ(cc_pack_item(&a, buf, 20), -1);               // buffer too small to pack

  // run and history records
  CcRun run = { 7, 20749, 1, 2, 4, 1, 1792738860, 1792738800, 40, { 0, 1, 2, 3 }, 12, 2 }, run2;
  n = cc_pack_run(&run, buf, sizeof(buf));
  T_TRUE(n > 0 && n <= 256);
  T_TRUE(cc_unpack_run(buf, n, &run2));
  T_EQ(run2.item_id, 7); T_EQ(run2.day, 20749); T_EQ(run2.active, 1); T_EQ(run2.step, 2);
  T_EQ(run2.step_count, 4); T_EQ(run2.paused, 1); T_EQ(run2.step_end, 1792738860);
  T_EQ(run2.advanced_at, 1792738800); T_EQ(run2.paused_left, 40); T_EQ(run2.value, 12); T_EQ(run2.swapped, 2);
  buf[0] = 9;
  T_TRUE(!cc_unpack_run(buf, n, &run2));

  CcOutcome oc[CC_MAX_ITEMS], oc2[CC_MAX_ITEMS];
  for (int i = 0; i < CC_MAX_ITEMS; i++) oc[i] = (CcOutcome){ (uint16_t)(i + 1), (uint8_t)(i % 6), (uint8_t)i, (uint8_t)(i / 2) };
  n = cc_pack_history(20749, oc, CC_MAX_ITEMS, buf, sizeof(buf));
  T_TRUE(n > 0 && n <= 256);
  int32_t hday = 0;
  T_EQ(cc_unpack_history(buf, n, &hday, oc2, CC_MAX_ITEMS), CC_MAX_ITEMS);
  T_EQ(hday, 20749);
  bool same = true;
  for (int i = 0; i < CC_MAX_ITEMS; i++)
    same = same && oc[i].id == oc2[i].id && oc[i].status == oc2[i].status &&
           oc[i].count == oc2[i].count && oc[i].missed == oc2[i].missed;
  T_TRUE(same);
  T_EQ(cc_unpack_history(buf, n, &hday, oc2, 3), -1);  // more than the caller can hold

  // Focus round trip, paused mid-way through a short break
  CcFocus fo, fo2;
  memset(&fo, 0, sizeof(fo));
  fo.active = 1; fo.phase = CC_FOCUS_SHORT; fo.cycle = 3; fo.cycles = 4; fo.paused = 1;
  fo.work_min = 50; fo.short_min = 10; fo.long_min = 20; fo.label_id = 12;
  fo.phase_end = CC_NEVER; fo.paused_left = 412;
  n = cc_pack_focus(&fo, buf, sizeof(buf));
  T_TRUE(n > 0 && n <= 256);
  memset(&fo2, 0xAB, sizeof(fo2));
  T_TRUE(cc_unpack_focus(buf, n, &fo2));
  T_TRUE(memcmp(&fo, &fo2, sizeof(fo)) == 0);
  buf[0] = 9;
  T_TRUE(!cc_unpack_focus(buf, n, &fo2));
}
