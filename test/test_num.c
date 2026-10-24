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
  T_EQ(cc_num_step(824, &w, 1, 15), 829);
  T_EQ(cc_num_step(824, &w, 1, 40), 834);
  T_EQ(cc_num_step(2498, &w, 1, 40), 2500);
  T_EQ(cc_num_step(305, &w, -1, 40), 300);
  T_EQ(cc_num_step(824, &w, -1, 9), 823);
  T_EQ(cc_num_step(100, &w, 1, 0), 300);  // out of range values are pulled in
}
