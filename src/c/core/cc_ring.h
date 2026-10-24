#pragma once
// A ring of persist slots: the newest overwrites the oldest when full.
#include "cc_types.h"

typedef struct { uint16_t head, count; } CcRing;  // head = next slot to write

uint16_t cc_ring_push(CcRing *r, uint16_t capacity);                // slot to write
uint16_t cc_ring_slot(const CcRing *r, uint16_t capacity, uint16_t i);  // slot of the i-th oldest
