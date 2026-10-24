#include "cc_ring.h"

uint16_t cc_ring_push(CcRing *r, uint16_t capacity) {
  uint16_t slot = r->head % capacity;
  r->head = (uint16_t)((slot + 1) % capacity);
  if (r->count < capacity) r->count++;
  return slot;
}

uint16_t cc_ring_slot(const CcRing *r, uint16_t capacity, uint16_t i) {
  uint16_t oldest = (uint16_t)((r->head + capacity - r->count) % capacity);
  return (uint16_t)((oldest + i) % capacity);
}
