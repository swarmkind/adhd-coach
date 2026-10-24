#include "fittext.h"
#include "../core/cc_codec.h"

#define TICK_MS 40
#define PAUSE_START_MS 1000
#define PAUSE_END_MS 800
#define SPEED_PX_S 30
#define LOOPS 3
#define END_PAD 4  // keep the last letter off the edge

typedef enum { FIT_BIG, FIT_SMALL, FIT_MARQUEE, FIT_REST } FitMode;
typedef enum { PH_START, PH_SCROLL, PH_END } Phase;

struct FitText {
  Layer *layer;
  GFont big, small;
  uint8_t max_lines;
  GColor fg;
  char text[FITTEXT_MAX];
  FitMode mode;
  Phase phase;
  int16_t text_w, offset;
  int phase_ms, loops;
  AppTimer *timer;
};

static GSize measure(const char *s, GFont font, int w, int h) {
  return graphics_text_layout_get_content_size(s, font, GRect(0, 0, w, h), GTextOverflowModeWordWrap,
                                               GTextAlignmentLeft);
}

static int line_h(GFont font) { return measure("Ag", font, 1000, 1000).h; }

// Fits in `lines` lines of `w` without breaking a word?
static bool fits(const char *s, GFont font, int w, int lines) {
  if (lines <= 1) return measure(s, font, 2000, 1000).w <= w;
  char word[FITTEXT_MAX];
  for (const char *p = s; *p;) {
    while (*p == ' ') p++;
    int n = 0;
    while (p[n] && p[n] != ' ' && n < FITTEXT_MAX - 1) { word[n] = p[n]; n++; }
    word[n] = 0;
    p += n;
    if (n && measure(word, font, 2000, 1000).w > w) return false;
  }
  int h1 = line_h(font);
  return measure(s, font, w, 1000).h < h1 * lines + h1 / 2;
}

static void stop(FitText *f) {
  if (f->timer) { app_timer_cancel(f->timer); f->timer = NULL; }
}

static void tick(void *data) {
  FitText *f = data;
  f->timer = NULL;
  int w = layer_get_bounds(f->layer).size.w;
  f->phase_ms += TICK_MS;
  switch (f->phase) {
    case PH_START:
      if (f->phase_ms >= PAUSE_START_MS) { f->phase = PH_SCROLL; f->phase_ms = 0; }
      break;
    case PH_SCROLL: {
      int end = f->text_w + END_PAD - w;
      f->offset = (int16_t)(f->phase_ms * SPEED_PX_S / 1000);
      if (f->offset >= end) { f->offset = (int16_t)end; f->phase = PH_END; f->phase_ms = 0; }
      break;
    }
    case PH_END:
      if (f->phase_ms >= PAUSE_END_MS) {
        f->offset = 0;
        f->phase = PH_START;
        f->phase_ms = 0;
        if (++f->loops >= LOOPS) f->mode = FIT_REST;
      }
      break;
  }
  layer_mark_dirty(f->layer);
  if (f->mode == FIT_MARQUEE) f->timer = app_timer_register(TICK_MS, tick, f);
}

static void update(Layer *layer, GContext *ctx) {
  FitText *f = *(FitText **)layer_get_data(layer);
  GRect b = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, f->fg);
  switch (f->mode) {
    case FIT_BIG:
      graphics_draw_text(ctx, f->text, f->big, b, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      break;
    case FIT_SMALL:
    case FIT_REST:
      b.size.h = (int16_t)(line_h(f->small) + 4);  // one line, so the ellipsis lands on it
      graphics_draw_text(ctx, f->text, f->small, b, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      break;
    case FIT_MARQUEE:
      graphics_draw_text(ctx, f->text, f->small, GRect(-f->offset, 0, f->text_w + END_PAD + 8, b.size.h),
                         GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
      break;
  }
}

void fittext_restart(FitText *f) {
  stop(f);
  int w = layer_get_bounds(f->layer).size.w;
  if (fits(f->text, f->big, w, f->max_lines)) f->mode = FIT_BIG;
  else if (fits(f->text, f->small, w, 1)) f->mode = FIT_SMALL;
  else f->mode = FIT_MARQUEE;
  f->text_w = (int16_t)measure(f->text, f->small, 2000, 1000).w;
  f->phase = PH_START;
  f->phase_ms = 0;
  f->offset = 0;
  f->loops = 0;
  if (f->mode == FIT_MARQUEE) f->timer = app_timer_register(TICK_MS, tick, f);
  layer_mark_dirty(f->layer);
}

FitText *fittext_create(GRect frame, GFont big, GFont small, uint8_t max_lines, GColor fg) {
  FitText *f = malloc(sizeof(FitText));
  if (!f) return NULL;
  memset(f, 0, sizeof(*f));
  f->layer = layer_create_with_data(frame, sizeof(FitText *));
  *(FitText **)layer_get_data(f->layer) = f;
  layer_set_update_proc(f->layer, update);
  f->big = big; f->small = small; f->max_lines = max_lines ? max_lines : 1; f->fg = fg;
  return f;
}

void fittext_set(FitText *f, const char *text) {
  strncpy(f->text, text ? text : "", FITTEXT_MAX - 1);
  f->text[FITTEXT_MAX - 1] = 0;
  cc_utf8_trunc(f->text, FITTEXT_MAX);
  fittext_restart(f);
}

Layer *fittext_layer(FitText *f) { return f->layer; }

void fittext_destroy(FitText *f) {
  if (!f) return;
  stop(f);
  layer_destroy(f->layer);
  free(f);
}
