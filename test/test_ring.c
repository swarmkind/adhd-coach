#include "t.h"
#include "cc_ring.h"

void test_ring(void) {
  CcRing r = { 0, 0 };
  T_EQ(cc_ring_push(&r, 3), 0);
  T_EQ(cc_ring_push(&r, 3), 1);
  T_EQ(cc_ring_push(&r, 3), 2);
  T_EQ(cc_ring_push(&r, 3), 0);  // full: overwrites the oldest
  T_EQ(r.count, 3);
  T_EQ(cc_ring_slot(&r, 3, 0), 1);  // oldest
  T_EQ(cc_ring_slot(&r, 3, 2), 0);  // newest
  CcRing e = { 0, 0 };
  T_EQ(cc_ring_push(&e, 90), 0);
  T_EQ(cc_ring_slot(&e, 90, 0), 0);
}
