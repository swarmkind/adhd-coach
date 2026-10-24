#pragma once
// Touch on Coach's own screens. A tap on the rail acts as the button beside it, and a hold on
// the Select rail as a long Select; a screen can also take taps on its body. One touch
// subscription serves whichever registered window is in front. Lists, menus and transcripts
// get touch from the system's touch navigation instead.
#include <pebble.h>

typedef struct {
  ClickHandler up, select, down, select_long;  // called with a NULL recognizer and context
  void (*body)(int x, int y);                  // a tap left of the rail
} RailTaps;

// keep_bridge: leave the system's touch navigation on for this window (it has a ScrollLayer).
void railtap_register(Window *w, const RailTaps *taps, bool keep_bridge);
void railtap_init(void);
void railtap_deinit(void);
