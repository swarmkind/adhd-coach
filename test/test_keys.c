#include "t.h"
#include "cc_keys.h"
#include "cc_note.h"
#include "cc_codec.h"

typedef struct { const char *name; long first, n; } Range;

// Every record the watch keeps, as [first, first + n).
void test_keys(void) {
  const Range r[] = {
    { "settings", K_SETTINGS, 1 }, { "schedule", K_SCHEDULE, 1 },
    { "day", K_DAY, K_DAY_N }, { "run", K_RUN, 1 }, { "focus", K_FOCUS, 1 }, { "last", K_LAST, K_LAST_N },
    { "log ring", K_LOG_RING, 1 }, { "hist ring", K_HIST_RING, 1 }, { "note tombs", K_NOTE_TOMB, 1 },
    { "sync", K_SYNC, 1 }, { "sounds", K_SOUNDS, 1 }, { "alarms", K_ALARMS, 1 }, { "todos", K_TODOS, 1 },
    { "items", K_ITEM, CC_MAX_ITEMS }, { "1.4.1 steps", K_STEP, CC_MAX_ITEMS * K_STEP_V1 },
    { "history", K_HIST, K_HIST_N }, { "logs", K_LOG, K_LOG_N },
    { "notes", K_NOTE, K_NOTE_SLOTS * CC_NOTE_PARTS_MAX }, { "steps", K_STEP2, CC_MAX_ITEMS * CC_ITEM_STEPS },
  };
  int n = (int)(sizeof(r) / sizeof(r[0]));
  T_TRUE(K_DAY_N * CC_DAY_CHUNK >= CC_MAX_ITEMS);
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++) {
      bool apart = r[i].first + r[i].n <= r[j].first || r[j].first + r[j].n <= r[i].first;
      if (!apart) printf("keys: %s and %s overlap\n", r[i].name, r[j].name);
      T_TRUE(apart);
    }
}
