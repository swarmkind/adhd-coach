#include <pebble.h>
#include "voice.h"
#include "notes.h"
#include "buzz.h"
#include "store.h"
#include "../coach.h"
#include "../config.h"
#include "../core/cc_note.h"

typedef enum { V_IDLE, V_NOTE, V_TITLE } Mode;

// A session that never answers would hold the one slot and silence every later note, so it gets
// this long (listening and the phone's transcription) before it counts as failed.
#define DEADLINE_MS (180 * 1000)

#if !COACH_FAKE_DICTATION
static DictationSession *s_session;
#endif
static Mode s_mode;
static VoiceDone s_done;
static void (*s_got)(const char *text);
static uint32_t s_start;
static AppTimer *s_deadline;
#define TEXT_BUF (CC_NOTE_TEXT_MAX + 2)  // one byte past the limit, so the clip sees a split character

// The parked routine, else the running Focus label, else a task started today.
static uint16_t linked_item(void) {
  if (g_run.active) return g_run.item_id;
  if (g_focus.active && g_focus.label_id) return g_focus.label_id;
  for (int i = 0; i < g_day.n; i++) {
    const CcItem *it = coach_item(g_day.s[i].id);
    if (it && it->kind == CC_TASK && g_day.s[i].status == CC_ST_ACTIVE) return it->id;
  }
  return 0;
}

static void finish(DictationSessionStatus st, const char *text) {
  if (s_deadline) { app_timer_cancel(s_deadline); s_deadline = NULL; }
  Mode mode = s_mode;
  s_mode = V_IDLE;
  bool ok = st == DictationSessionStatusSuccess && text && text[0];
  if (mode == V_TITLE) {
    if (!ok) buzz_missed();
    if (s_got) s_got(ok ? text : NULL);
    return;
  }
  if (mode != V_NOTE) return;
  if (st == DictationSessionStatusFailureTranscriptionRejected) {  // Back out of listening
    APP_LOG(APP_LOG_LEVEL_INFO, "note cancelled");
    if (s_done) s_done(false);
    return;
  }
  CcNote n;
  memset(&n, 0, sizeof(n));
  n.id = s_start;
  uint32_t now = (uint32_t)time(NULL);
  n.len_s = (uint16_t)(now > s_start ? now - s_start : 0);
  n.link = linked_item();
  char *buf = malloc(TEXT_BUF);  // on the heap: the app image must stay under 64 KB
  if (!buf) ok = false;
  else buf[0] = 0;
  if (ok) {
    strncpy(buf, text, TEXT_BUF - 1);
    buf[TEXT_BUF - 1] = 0;
    n.text_len = cc_note_clip(buf);
    n.status = CC_NOTE_OK;
  } else {
    n.status = CC_NOTE_MISSED;
    n.reason = (uint8_t)(st == DictationSessionStatusSuccess ? DictationSessionStatusFailureNoSpeechDetected : st);
  }
  bool saved = notes_add(&n, buf ? buf : "");
  free(buf);
  APP_LOG(APP_LOG_LEVEL_INFO, "note saved id %u status %d len %d link %d text %d", (unsigned)n.id, n.status,
          n.len_s, n.link, n.text_len);
  if (n.status == CC_NOTE_OK) buzz_tick();
  else buzz_missed();
  if (s_done) s_done(saved);
}

static void deadline(void *data) {
  s_deadline = NULL;
  APP_LOG(APP_LOG_LEVEL_WARNING, "dictation: no answer in %d s", DEADLINE_MS / 1000);
  finish(DictationSessionStatusFailureSystemAborted, NULL);
#if !COACH_FAKE_DICTATION
  if (s_session) dictation_session_stop(s_session);  // a late answer now finds nothing waiting
#endif
}

#if !COACH_FAKE_DICTATION
static void dictated(DictationSession *session, DictationSessionStatus st, char *text, void *ctx) {
  finish(st, text);
}
#else
// Demo builds: no phone dictation in the emulator, so the callback gets canned results in turn.
static void fake_result(void *data) {
  static int n;
  static const char *long_text =
    "Idea for the morning routine: put the weigh-in before the stretches, because after the "
    "stretches I always forget it. Also try a shorter plank, forty five seconds instead of a full "
    "minute, and add a glass of water as its own step so the water reminder starts at one. Ask "
    "the dentist about the cracked filling on the left side, and book the car in for its service "
    "before the end of the month.";
  static char longer[3 * 400];  // past CC_NOTE_TEXT_MAX, so the clip and all four records run
  if (!longer[0]) snprintf(longer, sizeof(longer), "%s %s %s", long_text, long_text, long_text);
  switch (n++ % 3) {
    case 0: finish(DictationSessionStatusSuccess, longer); break;
    case 1: finish(DictationSessionStatusSuccess, s_mode == V_TITLE ? "Garden ideas" : "Buy oat milk"); break;
    default: finish(DictationSessionStatusFailureConnectivityError, NULL); break;
  }
}
#endif

static void begin(Mode mode) {
  if (s_mode != V_IDLE) return;  // one session at a time
  s_mode = mode;
  s_start = (uint32_t)time(NULL);
  s_deadline = app_timer_register(DEADLINE_MS, deadline, NULL);
#if COACH_FAKE_DICTATION
  app_timer_register(1500, fake_result, NULL);
#else
  if (!s_session) {
    s_session = dictation_session_create(0, dictated, NULL);
    if (s_session) {
      dictation_session_enable_confirmation(s_session, false);  // spec 4.6: no "is this right?"
      dictation_session_enable_error_dialogs(s_session, false);  // a failure becomes a missed note
    }
  }
  if (!s_session) {  // no phone app connected, or it has no dictation
    finish(DictationSessionStatusFailureConnectivityError, NULL);
    return;
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "dictation start -> %d", (int)dictation_session_start(s_session));
#endif
}

void voice_note_start(VoiceDone done) {
  s_done = done;
  begin(V_NOTE);
}

void voice_dictate_text(void (*got)(const char *text)) {
  s_got = got;
  begin(V_TITLE);
}

const char *voice_reason_text(uint8_t reason) {
  switch (reason) {
    case DictationSessionStatusFailureConnectivityError: return "phone not reachable";
    case DictationSessionStatusFailureDisabled: return "dictation is off";
    case DictationSessionStatusFailureNoSpeechDetected: return "no speech heard";
    default: return "transcription failed";
  }
}
