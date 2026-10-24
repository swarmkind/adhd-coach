#pragma once
// Poster look: the screen takes the colour of its kind; a black rail on the right
// carries one glyph and one word per button.
#include <pebble.h>
#include "../core/cc_types.h"

#define POSTER_RAIL_W 38
#define POSTER_TEXT_X 12
#define POSTER_TEXT_W 142
#define POSTER_Y_UP 52
#define POSTER_Y_SELECT 114
#define POSTER_Y_DOWN 176

GColor poster_kind_color(uint8_t kind);  // CcKind, or POSTER_FOCUS / POSTER_NOTHING
#define POSTER_FOCUS 100
#define POSTER_NOTHING 101
#define POSTER_VOICE 102
#define POSTER_ALARM 103

typedef enum { G_NONE, G_LATER, G_DONE, G_SKIP, G_START, G_PLUS, G_MINUS, G_TODAY, G_FOCUS, G_NOTE, G_UNDO, G_RESUME, G_PAUSE, G_UP, G_DOWN, G_STOP, G_ALARM } Glyph;
typedef struct { Glyph glyph; const char *label; } RailAction;

void poster_draw_background(GContext *ctx, GRect bounds, GColor color);
void poster_draw_rail(GContext *ctx, GRect bounds, GColor accent, const RailAction actions[3]);  // Up, Select, Down
void poster_draw_back_hint(GContext *ctx, const char *label);  // "< label" at the top left
void poster_draw_glyph(GContext *ctx, Glyph g, GPoint centre, GColor color);

void poster_fmt_time(uint16_t min_of_day, char *buf, int len);  // "07:00", or "7:00 am" in 12 h style
void poster_fmt_clock(uint16_t min_of_day, char *buf, int len); // "07:00", or "7:00": digits only
const char *poster_wday(int32_t day);                       // "Sat"
void poster_fmt_date(int32_t day, char *buf, int len);      // "Sat 31 Oct"
