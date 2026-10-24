#pragma once
// Alarms set on the watch: a time of day and the days it rings on (none = once). A ring stays
// due until stopped or snoozed, so the screen and the wakeups come back to it.
#include "cc_types.h"
#include "cc_fires.h"

#define CC_ALARM_MAX 6
#define CC_ALARM_LATE (30 * 60)  // a ring missed with the watch off still sounds this late
#define CC_ALARM_SNOOZE_MIN 10

typedef enum { CC_ALARM_SOUND, CC_ALARM_VIBRATE } CcAlarmMode;  // sound and vibration, or vibration only

typedef struct {
  uint16_t at_min;
  uint8_t days, on;  // days: bit0 = Sunday ... bit6 = Saturday; 0 = once
  cc_sec handled;    // rings at or before this are dealt with
  cc_sec snooze;     // 0, or when a snoozed ring sounds again
  uint8_t mode;      // CcAlarmMode; last, so 0.7.0's 12-byte alarms are a prefix of these
} CcAlarm;

typedef struct { uint8_t n; CcAlarm a[CC_ALARM_MAX]; } CcAlarms;

cc_sec cc_alarm_next(const CcAlarm *a, cc_sec now);  // CC_NEVER when off
void cc_alarm_set(CcAlarm *a, uint16_t at_min, uint8_t days, cc_sec now);  // on, counting from now; keeps the mode
void cc_alarm_stop(CcAlarm *a, cc_sec now);    // a once alarm turns off
void cc_alarm_snooze(CcAlarm *a, cc_sec now);
void cc_alarm_remove(CcAlarms *l, int i);

// Alarms are saved as the struct itself. Reads this version's or 0.7.0's (sound alarms);
// anything else is no alarms, and false.
bool cc_alarms_read(CcAlarms *l, const uint8_t *buf, int len);

// Adds each alarm's next ring to fires sorted by time (CC_FIRE_ALARM, critical, id = index).
int cc_alarm_fires(const CcAlarms *l, cc_sec now, CcFire f[], int n, int max);

// The editor's day choices: once, every day, weekdays, weekends, then Mondays .. Sundays.
#define CC_DAYS_PRESETS 11
uint8_t cc_days_preset(int i);
int cc_days_index(uint8_t days);  // 0 when not a choice
const char *cc_days_name(uint8_t days);
