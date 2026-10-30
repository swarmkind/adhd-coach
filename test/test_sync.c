#include "t.h"
#include "cc_sync.h"
#include "cc_codec.h"

static void test_frames(void) {
  uint8_t buf[64];
  const uint8_t a[] = { 1, 2, 3 }, b[] = { 9 }, c[] = { 7, 7, 7, 7, 7 };
  int pos = cc_frame_put(buf, sizeof(buf), 0, CC_T_ITEM, a, 3);
  T_EQ(pos, 6);
  pos = cc_frame_put(buf, sizeof(buf), pos, CC_T_STEP, b, 1);
  pos = cc_frame_put(buf, sizeof(buf), pos, CC_T_LOG, c, 5);
  T_EQ(pos, 6 + 4 + 8);
  T_EQ(buf[0], CC_T_ITEM); T_EQ(buf[1], 3); T_EQ(buf[2], 0);

  uint8_t type; const uint8_t *rec; int rlen;
  int p = cc_frame_next(buf, pos, 0, &type, &rec, &rlen);
  T_EQ(p, 6); T_EQ(type, CC_T_ITEM); T_EQ(rlen, 3); T_EQ(rec[2], 3);
  p = cc_frame_next(buf, pos, p, &type, &rec, &rlen);
  T_EQ(type, CC_T_STEP); T_EQ(rlen, 1); T_EQ(rec[0], 9);
  p = cc_frame_next(buf, pos, p, &type, &rec, &rlen);
  T_EQ(type, CC_T_LOG); T_EQ(rlen, 5);
  T_EQ(cc_frame_next(buf, pos, p, &type, &rec, &rlen), 0);  // the end

  // A frame that would pass the cap is refused and the buffer stays usable.
  uint8_t small[10];
  int q = cc_frame_put(small, sizeof(small), 0, CC_T_ITEM, a, 3);
  T_EQ(cc_frame_put(small, sizeof(small), q, CC_T_LOG, c, 5), -1);
  T_EQ(cc_frame_put(small, sizeof(small), q, CC_T_STEP, b, 1), q + 4);

  T_EQ(cc_frame_next(buf, 5, 0, &type, &rec, &rlen), -1);  // header says 3 bytes, only 2 there
  T_EQ(cc_frame_next(buf, 2, 0, &type, &rec, &rlen), -1);  // a torn header
}

static void test_records(void) {
  uint8_t buf[256];
  CcStep s; memset(&s, 0, sizeof(s));
  s.type = CC_STEP_NUMBER; s.num = (CcNumSpec){ 1, 300, 2500, 1, "kg" };
  strcpy(s.name, "Weight");
  int n = cc_pack_step_at(7, 2, &s, buf, sizeof(buf));
  T_TRUE(n > 3);
  uint16_t item; uint8_t idx; CcStep back;
  T_TRUE(cc_unpack_step_at(buf, n, &item, &idx, &back));
  T_EQ(item, 7); T_EQ(idx, 2); T_STR(back.name, "Weight"); T_EQ(back.num.max, 2500); T_STR(back.num.unit, "kg");
  T_TRUE(!cc_unpack_step_at(buf, 2, &item, &idx, &back));

  CcSettings c = CC_SETTINGS_DEFAULT, cb;
  c.focus_work_min = 30;
  uint32_t utc = 0;
  n = cc_pack_settings_at(&c, 1761200000u, buf, sizeof(buf));
  T_TRUE(cc_unpack_settings_at(buf, n, &cb, &utc));
  T_EQ(utc, 1761200000u); T_EQ(cb.focus_work_min, 30); T_EQ(cb.event_time_min, 540);

  uint32_t ids[60], got[60];
  for (int i = 0; i < 60; i++) ids[i] = 1792800000u + (uint32_t)i;
  T_EQ(cc_unpack_tombs(buf, cc_pack_tombs(ids, 0, buf, sizeof(buf)), got, 60), 0);
  T_EQ(cc_unpack_tombs(buf, cc_pack_tombs(ids, 1, buf, sizeof(buf)), got, 60), 1);
  T_EQ(got[0], 1792800000u);
  n = cc_pack_tombs(ids, 60, buf, sizeof(buf));
  T_EQ(n, 1 + 240);
  T_EQ(cc_unpack_tombs(buf, n, got, 60), 60);
  T_EQ(got[59], 1792800059u);
  T_EQ(cc_unpack_tombs(buf, n, got, 59), -1);  // more than the caller can hold
  T_EQ(cc_unpack_tombs(buf, n - 1, got, 60), -1);

  // 30 ASCII bytes then "é" (2 bytes) passes byte 31: the cut falls before it.
  char name[CC_NAME_MAX];
  uint32_t id;
  n = cc_pack_rename(1792800001u, "abcdefghijklmnopqrstuvwxyz0123\xC3\xA9", buf, sizeof(buf));
  T_TRUE(cc_unpack_rename(buf, n, &id, name));
  T_EQ(id, 1792800001u); T_STR(name, "abcdefghijklmnopqrstuvwxyz0123");
  n = cc_pack_rename(5, "Garden ideas", buf, sizeof(buf));
  T_TRUE(cc_unpack_rename(buf, n, &id, name));
  T_STR(name, "Garden ideas");
  T_TRUE(!cc_unpack_rename(buf, n - 1, &id, name));

  CcSync sy = { 3, 0xA1B2C3D4u, 1761200000u, 1761200100u }, sb;
  n = cc_pack_sync(&sy, buf, sizeof(buf));
  T_TRUE(cc_unpack_sync(buf, n, &sb));
  T_EQ(sb.rev, 3); T_EQ(sb.phone_id, 0xA1B2C3D4u); T_EQ(sb.settings_utc, 1761200000u); T_EQ(sb.last_sync_utc, 1761200100u);
  buf[0] = 9;
  T_TRUE(!cc_unpack_sync(buf, n, &sb));
}

static void test_prune_and_stage(void) {
  CcDay d; memset(&d, 0, sizeof(d));
  d.n = 3;
  d.s[0].id = 1; d.s[1].id = 2; d.s[2].id = 3; d.s[2].count = 5;
  CcItem items[2]; memset(items, 0, sizeof(items));
  items[0].id = 1; items[1].id = 3;
  T_EQ(cc_day_prune(&d, items, 2), 1);
  T_EQ(d.n, 2); T_EQ(d.s[0].id, 1); T_EQ(d.s[1].id, 3); T_EQ(d.s[1].count, 5);
  T_EQ(cc_day_prune(&d, items, 2), 0);

  uint64_t mask[2] = { 0x7, 0 };
  items[0].step_count = 4; items[1].step_count = 0;
  T_TRUE(!cc_stage_complete(items, 2, mask));  // step 3 of item 0 never came
  mask[0] = 0xF;
  T_TRUE(cc_stage_complete(items, 2, mask));
  items[1].step_count = 12; mask[1] = 0xFFF;
  T_TRUE(cc_stage_complete(items, 2, mask));
  items[1].step_count = 20; items[1].alt_count = 20; mask[1] = 0xFFFFFFFFFull;  // 36 of 40
  T_TRUE(!cc_stage_complete(items, 2, mask));
  mask[1] = 0xFFFFFFFFFFull;
  T_TRUE(cc_stage_complete(items, 2, mask));
}

void test_sync(void) {
  test_frames();
  test_records();
  test_prune_and_stage();
}
