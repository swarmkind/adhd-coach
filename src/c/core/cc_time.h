#pragma once
#include "cc_types.h"

int32_t cc_day_of(cc_sec t);              // local day number (floor)
uint16_t cc_min_of(cc_sec t);             // minute of day, 0..1439
uint8_t cc_weekday(int32_t day);          // 0 = Sunday
cc_sec cc_at(int32_t day, uint16_t min);  // start of that minute
void cc_date_of(int32_t day, int *year, int *month, int *mday);  // month 1..12
cc_sec cc_local_from_utc(int32_t utc, int32_t tz_offset_s);
