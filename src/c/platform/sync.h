#pragma once
// Sync with PebbleKit JS over AppMessage. The phone drives it (protocol table in docs/dev.md):
// the watch uploads what is new, then takes a schedule and note edits from the planner.
#include <pebble.h>

void sync_init(void);              // after store_load, before any window
void sync_settings_changed(void);  // a setting was saved on the watch: its copy is the newest now
uint32_t sync_last_utc(void);      // last finished upload, 0 = never
// Something new for the phone: ask it for a sync once things settle, and again later (on reconnect, then
// every so often) until an upload that began after this has been acknowledged.
void sync_poke(void);
// Runs `fn` once an upload with everything poked so far is acknowledged (asking at once), or after `cap_ms`;
// after 600 ms when nothing waits or the phone is away. For leaving the app without leaving data behind.
void sync_then(AppTimerCallback fn, uint32_t cap_ms);
