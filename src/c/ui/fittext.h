#pragma once
// A text box that keeps text readable: `big` if it fits in max_lines, else `small` on one
// line, else `small` scrolling sideways (marquee) a few times, then resting with an ellipsis.
#include <pebble.h>

#define FITTEXT_MAX 64

typedef struct FitText FitText;

FitText *fittext_create(GRect frame, GFont big, GFont small, uint8_t max_lines, GColor fg);
void fittext_set(FitText *f, const char *text);  // copies; restarts the marquee
void fittext_restart(FitText *f);
Layer *fittext_layer(FitText *f);
void fittext_destroy(FitText *f);
