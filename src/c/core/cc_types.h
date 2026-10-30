#pragma once
// Coach core model. Pure C: no pebble.h, no malloc, no floats.
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Local seconds since 1970-01-01 00:00 local time.
typedef int32_t cc_sec;
#define CC_NEVER INT32_MAX
#define CC_DAY 86400

#define CC_NAME_MAX 32     // bytes incl. NUL, UTF-8
#define CC_UNIT_MAX 6
#define CC_MAX_ITEMS 40
#define CC_MAX_STEPS 20     // main steps per routine
#define CC_MAX_ALTS 20      // alternatives per routine, one per step at most
#define CC_ITEM_STEPS (CC_MAX_STEPS + CC_MAX_ALTS)
#define CC_POOL_MAX 240     // steps and alternatives across all items
#define CC_NO_ALT 255
#define CC_EVERY_MAX 30
#define CC_LOG_WEIGHT 0x40  // log step byte: the weight of step (byte & 0x3F)
#define CC_LADDER_MAX 6    // used from Plan 2

typedef enum { CC_ROUTINE, CC_REMINDER, CC_TASK, CC_EVENT } CcKind;
typedef enum { CC_NORMAL, CC_IMPORTANT, CC_CRITICAL } CcPriority;
enum { CC_F_SILENT = 1, CC_F_ANYTIME = 2, CC_F_ASK_NUMBER = 4 };
typedef enum { CC_STEP_MANUAL, CC_STEP_TIMED, CC_STEP_NUMBER } CcStepType;
enum { CC_SF_QTY = 1, CC_SF_WEIGHT = 2 };  // CcStep.flags, Do it steps only

// Values are fixed point, scaled by 10^decimals (82.4 kg = 824, decimals 1).
typedef struct {
  int16_t step;
  int32_t min, max;
  uint8_t decimals;
  char unit[CC_UNIT_MAX];
} CcNumSpec;

// A value that steps up on a schedule: start on day `since`, + inc every `every` days.
typedef struct {
  int32_t start;   // scaled like its field
  int32_t cap;     // stop at; 0 = none
  int32_t since;   // local day number
  int16_t inc;     // 0 = fixed
  uint16_t every;  // days between step-ups, >= 1 when inc != 0
} CcRamp;

typedef struct {
  uint8_t type, flags;
  uint8_t alt;          // index of its alternative among the item's steps, or CC_NO_ALT
  uint8_t show_every;   // 0 = every run; else every N days from show_since
  int32_t show_since;
  CcRamp amount;        // timed: length in seconds; Do it with CC_SF_QTY: the quantity
  CcRamp weight;        // Do it with CC_SF_WEIGHT: scaled by num.decimals, unit num.unit
  CcNumSpec num;        // number steps; on Do it steps, the weight's decimals and unit
  char name[CC_NAME_MAX];
} CcStep;

typedef struct {
  uint16_t id;
  uint8_t kind, priority, flags;
  uint8_t days;                          // bit0 = Sunday ... bit6 = Saturday
  uint16_t at_min, until_min, every_min; // reminder window: at..until, every N min
  uint8_t goal, grace_min, order, step_count;
  uint8_t alt_count;                     // steps [0, step_count) are main, then the alternatives
  uint8_t step_first;                    // pool index (cc_pool_layout), never stored
  uint8_t every_days;                    // 0 = on `days`; else every N days from every_since
  int32_t every_since;
  int32_t event_day;                     // CC_EVENT (Plan 2)
  int8_t ladder[CC_LADDER_MAX];
  uint8_t ladder_count;
  CcNumSpec num;                         // when CC_F_ASK_NUMBER
  char name[CC_NAME_MAX];
} CcItem;

typedef enum { CC_ST_NONE, CC_ST_ACTIVE, CC_ST_DONE, CC_ST_SKIPPED, CC_ST_MISSED, CC_ST_PAUSED } CcStatus;
enum { CC_SF_HEADSUP_SHOWN = 1 };

typedef struct {
  uint16_t id;
  uint8_t status, count, missed, auto_later_used, flags;
  cc_sec later_until, handled_until;
} CcItemState;

typedef struct {
  int32_t day;
  uint8_t n;
  CcItemState s[CC_MAX_ITEMS];
} CcDay;

typedef struct {
  uint8_t later_task_min, later_reminder_min;
  uint8_t headsup_important_min, headsup_critical_min;
  uint8_t catchup_min;
  uint16_t event_time_min;   // advance event rungs; 540 = 09:00
  uint8_t later_event_min;   // event Later ("later today")
  uint8_t focus_work_min, focus_short_min, focus_long_min, focus_cycles;
} CcSettings;
#define CC_SETTINGS_DEFAULT { 30, 10, 10, 15, 10, 540, 120, 25, 5, 15, 4 }

typedef struct {
  int32_t utc;
  uint16_t item;
  uint8_t step;
  int32_t value;
} CcLogEntry;
