#include <pebble.h>
#include "buzz.h"
#include "../core/cc_types.h"
#include "store.h"

#define PLAY(...) do { \
    static const uint32_t seg[] = { __VA_ARGS__ }; \
    vibes_enqueue_custom_pattern((VibePattern){ .durations = seg, .num_segments = ARRAY_LENGTH(seg) }); \
  } while (0)

static bool sound_ok(void) { return store_sounds_on() && !speaker_is_muted() && !quiet_time_is_active(); }

// A note: MIDI number (60 = middle C, 0 = rest) and length in ms.
#define N(m, ms) { .midi_note = (m), .waveform = SpeakerWaveformSine, .duration_ms = (ms), .velocity = 0 }
#define TONE(volume, ...) do { \
    static const SpeakerNote notes[] = { __VA_ARGS__ }; \
    if (sound_ok() && !speaker_play_notes(notes, ARRAY_LENGTH(notes), (volume))) APP_LOG(APP_LOG_LEVEL_WARNING, "sound: not played"); \
  } while (0)

void buzz_alert(uint8_t priority) {
  vibes_cancel();
  if (priority == CC_CRITICAL) {
    PLAY(400, 150, 150, 150, 400, 400, 400, 150, 150, 150, 400);  // twice
    TONE(100, N(84, 110), N(88, 110), N(91, 110), N(96, 220), N(0, 200), N(84, 110), N(88, 110), N(91, 110), N(96, 220));
  } else if (priority == CC_IMPORTANT) {
    PLAY(250, 150, 250, 150, 250);
    TONE(80, N(84, 110), N(88, 110), N(91, 200));
  } else {
    PLAY(200, 150, 200);
    TONE(70, N(88, 120), N(0, 60), N(93, 160));
  }
}

void buzz_headsup(void) {
  vibes_cancel();
  PLAY(80, 100, 80, 100, 300);
  TONE(60, N(81, 90), N(0, 70), N(81, 90), N(0, 70), N(88, 220));
}
void buzz_nudge(void) {
  vibes_cancel();
  PLAY(120);
  TONE(40, N(91, 90));
}
void buzz_tick(void) {
  PLAY(40);
  TONE(40, N(96, 40));
}
void buzz_step_done(void) {  // a step's timer reached zero: firm, with three beeps, as you may be mid-exercise
  vibes_cancel();
  PLAY(300, 150, 300);
  TONE(90, N(88, 150), N(0, 100), N(88, 150), N(0, 100), N(93, 300));
}
void buzz_missed(void) {
  PLAY(40, 120, 40);
  TONE(50, N(76, 80), N(0, 60), N(72, 140));  // falling: it didn't work
}

void buzz_focus(bool finished) {
  vibes_cancel();
  if (finished) {
    PLAY(500, 200, 500);
    TONE(70, N(84, 150), N(88, 150), N(91, 150), N(96, 320));
  } else {
    PLAY(500);
    TONE(70, N(86, 180), N(81, 280));  // a two-note chime
  }
}

// An alarm is mostly set for Quiet Time, so it rings through it. Its own sound choice outranks the
// Sounds switch (that is for cues); only the watch's mute silences it.
void buzz_alarm(bool sound) {
  vibes_cancel();
  PLAY(600, 200, 600, 200, 600);
  static const SpeakerNote notes[] = { N(93, 120), N(0, 80), N(93, 120), N(0, 80), N(93, 120), N(0, 80), N(93, 120) };
  if (sound && !speaker_is_muted()) speaker_play_notes(notes, ARRAY_LENGTH(notes), 100);
}
