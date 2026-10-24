#pragma once
// Sync with PebbleKit JS over AppMessage. The phone drives it (protocol table in docs/dev.md):
// the watch uploads what is new, then takes a schedule and note edits from the planner.
#include <stdint.h>

void sync_init(void);              // after store_load, before any window
void sync_settings_changed(void);  // a setting was saved on the watch: its copy is the newest now
uint32_t sync_last_utc(void);      // last finished upload, 0 = never
