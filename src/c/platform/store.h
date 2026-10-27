#pragma once
// The app model in RAM and its persist keys. One owner: everything persists through here.
#include "../core/cc_types.h"
#include "../core/cc_run.h"
#include "../core/cc_state.h"
#include "../core/cc_focus.h"
#include "../core/cc_sync.h"
#include "../core/cc_alarm.h"
#include "../core/cc_todo.h"

extern CcItem *g_items;                  // [CC_MAX_ITEMS] on the heap, like the steps
extern int g_nitems;
extern CcStep (*g_steps)[CC_MAX_STEPS];  // [CC_MAX_ITEMS] on the heap: the app image must stay under 64 KB
extern CcDay *g_day;                     // on the heap, like the items
extern CcRun g_run;
extern CcSettings g_cfg;
extern CcFocus g_focus;
extern CcAlarms g_alarms;
extern CcTodos *g_todos;                 // note to-dos, watch-only, on the heap

void store_load(void);  // writes the sample schedule on first run
void store_save_day(void);
void store_save_run(void);
void store_save_schedule(void);
void store_save_settings(void);
void store_save_focus(void);
void store_save_alarms(void);
void store_save_todos(void);

void store_log_number(uint16_t item, uint8_t step, int32_t value);
bool store_last_number(uint16_t item, uint8_t step, int32_t *value, int32_t *utc);
void store_history_push(const CcOutcome *o, int n, int32_t day);  // 90-day ring

// Phone sync (key 45) and raw records for upload, i-th oldest first; -1 when unreadable.
void store_load_sync(CcSync *s);
void store_save_sync(const CcSync *s);
bool store_sounds_on(void);  // the Settings switch, on until turned off
void store_set_sounds(bool on);
int store_log_chunks(void);
int store_log_chunk(int i, uint8_t *buf, int len);
int store_hist_days(void);
int store_hist_record(int i, uint8_t *buf, int len);
