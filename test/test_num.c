#include "t.h"
#include "cc_num.h"

void test_num(void) {
  char b[16];
  T_EQ(cc_num_format(824, 1, b, sizeof(b)), 4);  T_STR(b, "82.4");
  T_EQ(cc_num_format(-3, 1, b, sizeof(b)), 4);   T_STR(b, "-0.3");
  T_EQ(cc_num_format(10, 0, b, sizeof(b)), 2);   T_STR(b, "10");
  T_EQ(cc_num_format(5, 2, b, sizeof(b)), 4);    T_STR(b, "0.05");
  T_EQ(cc_num_format(0, 0, b, sizeof(b)), 1);    T_STR(b, "0");
  T_EQ(cc_num_format(-2500, 1, b, sizeof(b)), 6); T_STR(b, "-250.0");
  T_EQ(cc_num_format(2147483647, 0, b, sizeof(b)), 10); T_STR(b, "2147483647");
  T_EQ(cc_num_format(824, 1, b, 4), -1);         // "82.4" needs 5 bytes
  T_STR(b, "");

  CcNumSpec w = { 1, 300, 2500, 1, "kg" };
  T_EQ(cc_num_step(824, &w, 1, 0), 825);
  T_EQ(cc_num_step(824, &w, 1, 15), 826);
  T_EQ(cc_num_step(824, &w, 1, 25), 829);
  T_EQ(cc_num_step(824, &w, 1, 40), 834);
  T_EQ(cc_num_step(2498, &w, 1, 40), 2500);
  T_EQ(cc_num_step(305, &w, -1, 40), 300);
  T_EQ(cc_num_step(824, &w, -1, 9), 823);
  T_EQ(cc_num_step(100, &w, 1, 0), 300);  // out of range values are pulled in

  // Holding a button: one repeat every CC_NUM_REPEAT_MS, ramping up gently.
  CcNumSpec reps = { 1, 0, 9999, 0, "" };
  int32_t v = 0;
  T_EQ(CC_NUM_REPEAT_MS, 200);
  for (uint16_t k = 0; k < 10; k++) v = cc_num_step(v, &reps, 1, k);
  T_EQ(v, 10);                                       // 2 s held: +10
  for (uint16_t k = 10; k < 20; k++) v = cc_num_step(v, &reps, 1, k);
  T_EQ(v, 30);                                       // 4 s: +30
  for (uint16_t k = 20; k < 30; k++) v = cc_num_step(v, &reps, 1, k);
  T_EQ(v, 80);                                       // 6 s: +80, then 10 a repeat
}
