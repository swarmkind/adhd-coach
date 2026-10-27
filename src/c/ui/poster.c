#include "poster.h"
#include "../core/cc_time.h"
#include "../core/cc_types.h"
#include "../platform/clock.h"

#define DIGITS_W 56
#define COLON_W 14

GColor poster_kind_color(uint8_t kind) {
  switch (kind) {
    case CC_ROUTINE: return GColorChromeYellow;
    case CC_REMINDER: return GColorVividCerulean;
    case CC_TASK: return GColorBrilliantRose;
    case CC_EVENT: return GColorSunsetOrange;
    case POSTER_FOCUS: return GColorJaegerGreen;
    case POSTER_VOICE: return GColorLavenderIndigo;
    case POSTER_ALARM: return GColorElectricBlue;
    default: return GColorLightGray;
  }
}

void poster_draw_background(GContext *ctx, GRect bounds, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
}

// Glyphs live in a 16 x 16 box; `o` is its top-left corner.
static void line(GContext *ctx, GPoint o, int x1, int y1, int x2, int y2) {
  graphics_draw_line(ctx, GPoint(o.x + x1, o.y + y1), GPoint(o.x + x2, o.y + y2));
}

static void arc(GContext *ctx, GPoint o, int cx, int cy, int r, int from_deg, int to_deg) {
  graphics_draw_arc(ctx, GRect(o.x + cx - r, o.y + cy - r, 2 * r + 1, 2 * r + 1), GOvalScaleModeFitCircle,
                    DEG_TO_TRIGANGLE(from_deg), DEG_TO_TRIGANGLE(to_deg));
}

static void triangle(GContext *ctx, GPoint o, GPoint a, GPoint b, GPoint c) {
  GPoint pts[3] = { a, b, c };
  GPath p = { .num_points = 3, .points = pts, .rotation = 0, .offset = o };
  gpath_draw_filled(ctx, &p);
}

// Each glyph's lines as bytes, one string per glyph in Glyph order: a point is x << 4 | y (x is never 0),
// joined to the point before it; 1 lifts the pen and 2 or 3 sets the stroke width (and lifts it too).
static const char STROKES[] =
  "\0"                                       // G_NONE
  "\x13\x83\x1b\x8b\x02\xa8\xe8\xad\xed" "\0"  // G_LATER: Zz
  "\x29\x6d\xe4" "\0"                        // G_DONE
  "\xe8\xed\x9c" "\0"                        // G_SKIP
  "\0"                                       // G_START
  "\x82\x8e\x01\x28\xe8" "\0"                // G_PLUS
  "\x28\xe8" "\0"                            // G_MINUS
  "\0"                                       // G_TODAY
  "\x86\x89\xab\x01\x61\xa1" "\0"            // G_FOCUS
  "\x8d\x8f\x01\x5f\xbf" "\0"                // G_NOTE
  "\x28\x2d\x7c" "\0"                        // G_UNDO
  "\0"                                       // G_RESUME
  "\0"                                       // G_PAUSE
  "\x2b\x85\xeb" "\0"                        // G_UP
  "\x25\x8b\xe5" "\0"                        // G_DOWN
  "\0"                                       // G_STOP
  "\x37\x3c\x01\xd7\xdc\x01\x1c\xfc";        // G_ALARM

static void strokes(GContext *ctx, GPoint o, Glyph g) {
  const char *s = STROKES;
  for (int i = 0; i < (int)g; i++) s += strlen(s) + 1;
  GPoint prev = o;
  bool down = false;
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (c < 0x10) {
      if (c > 1) graphics_context_set_stroke_width(ctx, c);
      down = false;
      continue;
    }
    GPoint p = GPoint(o.x + (c >> 4), o.y + (c & 15));
    if (down) graphics_draw_line(ctx, prev, p);
    prev = p;
    down = true;
  }
}

void poster_draw_glyph(GContext *ctx, Glyph g, GPoint centre, GColor color) {
  GPoint o = GPoint(centre.x - 8, centre.y - 8);
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 3);
  switch (g) {  // the shapes under the lines
    case G_SKIP: arc(ctx, o, 8, 11, 6, -90, 75); break;  // arrow curving over to the right
    case G_UNDO: arc(ctx, o, 8, 11, 6, -75, 90); break;  // the same arrow, mirrored
    case G_START:
    case G_RESUME: triangle(ctx, o, GPoint(3, 1), GPoint(14, 8), GPoint(3, 15)); break;
    case G_PAUSE:
      graphics_fill_rect(ctx, GRect(o.x + 3, o.y + 2, 4, 12), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(o.x + 9, o.y + 2, 4, 12), 0, GCornerNone);
      break;
    case G_TODAY:  // list: three dots and three lines
      for (int y = 3; y <= 13; y += 5) {
        graphics_fill_rect(ctx, GRect(o.x + 1, o.y + y - 1, 3, 3), 0, GCornerNone);
        line(ctx, o, 7, y, 14, y);
      }
      break;
    case G_FOCUS:  // stopwatch
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_circle(ctx, GPoint(o.x + 8, o.y + 9), 6);
      break;
    case G_NOTE:  // microphone
      graphics_context_set_stroke_width(ctx, 2);
      graphics_fill_rect(ctx, GRect(o.x + 5, o.y, 7, 10), 3, GCornersAll);
      arc(ctx, o, 8, 6, 6, 90, 270);
      break;
    case G_STOP: graphics_fill_rect(ctx, GRect(o.x + 3, o.y + 3, 11, 11), 0, GCornerNone); break;
    case G_ALARM:  // bell
      graphics_context_set_stroke_width(ctx, 2);
      arc(ctx, o, 8, 7, 5, -90, 90);
      break;
    default: break;
  }
  if ((unsigned)g <= G_ALARM) strokes(ctx, o, g);
  if (g == G_ALARM) graphics_fill_rect(ctx, GRect(o.x + 6, o.y + 14, 5, 2), 0, GCornerNone);
}

void poster_draw_rail(GContext *ctx, GRect bounds, GColor accent, const RailAction actions[3]) {
  GRect rail = GRect(bounds.size.w - POSTER_RAIL_W, 0, POSTER_RAIL_W, bounds.size.h);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, rail, 0, GCornerNone);
  static const int ys[3] = { POSTER_Y_UP, POSTER_Y_SELECT, POSTER_Y_DOWN };
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  int cx = rail.origin.x + POSTER_RAIL_W / 2;
  for (int i = 0; i < 3; i++) {
    if (actions[i].glyph == G_NONE) continue;
    poster_draw_glyph(ctx, actions[i].glyph, GPoint(cx, ys[i] - 9), accent);
    if (!actions[i].label) continue;
    graphics_context_set_text_color(ctx, GColorWhite);
    // The layout box is 6 px wider than the rail: advance widths reach 35 px ("today") while the
    // ink, centred, stays inside the 34 px rail.
    graphics_draw_text(ctx, actions[i].label, f, GRect(rail.origin.x - 3, ys[i] - 2, POSTER_RAIL_W + 6, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

void poster_draw_back_hint(GContext *ctx, const char *label) {
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(15, 9), GPoint(12, 13));  // chevron: Gothic has no "‹"
  graphics_draw_line(ctx, GPoint(12, 13), GPoint(15, 17));
  graphics_context_set_text_color(ctx, GColorBlack);
  poster_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(19, 3, 80, 18));
}

void poster_text(GContext *ctx, const char *text, GFont f, GRect box) {
  graphics_draw_text(ctx, text, f, box, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

void poster_draw_row(GContext *ctx, const Layer *cell, GColor accent, bool strip, const char *title, const char *sub) {
  GRect b = layer_get_bounds(cell);
  bool hl = menu_cell_layer_is_highlighted(cell);
  graphics_context_set_fill_color(ctx, hl ? accent : GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  if (strip && !hl) {
    graphics_context_set_fill_color(ctx, accent);
    graphics_fill_rect(ctx, GRect(0, 6, 4, b.size.h - 12), 0, GCornerNone);
  }
  int w = b.size.w - 2 * POSTER_TEXT_X;
  graphics_context_set_text_color(ctx, hl ? GColorBlack : GColorWhite);
  if (title)
    poster_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(POSTER_TEXT_X, sub ? 0 : 10, w, 30));
  if (!sub) return;
  graphics_context_set_text_color(ctx, hl ? GColorBlack : GColorLightGray);
  poster_text(ctx, sub, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(POSTER_TEXT_X, 27, w, 22));
}

void poster_open_menu(ActionMenuLevel *root, GColor bg, ActionMenuDidCloseCb closed) {
  ActionMenuConfig cfg = {
    .root_level = root,
    .colors = { .background = bg, .foreground = GColorBlack },
    .align = ActionMenuAlignCenter,
    .did_close = closed,
  };
  action_menu_open(&cfg);
}

void poster_draw_field(GContext *ctx, const char *text, GFont f, GRect box, GTextAlignment al, bool focused) {
  graphics_draw_text(ctx, text, f, box, GTextOverflowModeTrailingEllipsis, al, NULL);
  if (!focused) return;
  int w = graphics_text_layout_get_content_size(text, f, box, GTextOverflowModeTrailingEllipsis, al).w;
  int x = al == GTextAlignmentRight ? box.origin.x + box.size.w - w : box.origin.x;
  graphics_fill_rect(ctx, GRect(x, box.origin.y + box.size.h + 4, w, 4), 0, GCornerNone);
}

void poster_draw_editor_head(GContext *ctx, const char *label) {
  poster_draw_back_hint(ctx, label);
  char t[12], line[16];
  poster_fmt_time(cc_min_of(clock_now_local()), t, sizeof(t));
  snprintf(line, sizeof(line), "now %s", t);
  graphics_draw_text(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(POSTER_TEXT_X, 3, POSTER_TEXT_W, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

void poster_draw_editor_clock(GContext *ctx, int m, int y, int focus) {
  bool h24 = clock_is_24h_style();
  int h = m / 60;
  char hh[4], mm[4];
  snprintf(hh, sizeof(hh), h24 ? "%02d" : "%d", h24 ? h : h % 12 ? h % 12 : 12);
  snprintf(mm, sizeof(mm), "%02d", m % 60);
  GFont big = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  int x = POSTER_TEXT_X, mx = x + DIGITS_W + COLON_W;
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  poster_draw_field(ctx, hh, big, GRect(x, y, DIGITS_W, 48), GTextAlignmentRight, focus == POSTER_CLOCK_HOUR);
  poster_draw_field(ctx, ":", big, GRect(x + DIGITS_W, y, COLON_W, 48), GTextAlignmentCenter, false);
  poster_draw_field(ctx, mm, big, GRect(mx, y, DIGITS_W, 48), GTextAlignmentLeft, focus == POSTER_CLOCK_MIN);
  if (!h24) poster_draw_field(ctx, h < 12 ? "am" : "pm", fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                              GRect(mx + DIGITS_W, y + 20, 30, 22), GTextAlignmentLeft, false);
}

void poster_fmt_clock(uint16_t m, char *buf, int len) {
  int h = m / 60;
  if (clock_is_24h_style()) snprintf(buf, len, "%02d:%02d", h, m % 60);
  else snprintf(buf, len, "%d:%02d", h % 12 ? h % 12 : 12, m % 60);
}

void poster_fmt_time(uint16_t m, char *buf, int len) {
  poster_fmt_clock(m, buf, len);
  if (!clock_is_24h_style()) strncat(buf, m < 720 ? " am" : " pm", len - strlen(buf) - 1);
}

const char *poster_wday(int32_t day) {
  static const char *names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  return names[cc_weekday(day)];
}

void poster_fmt_date(int32_t day, char *buf, int len) {
  static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  int y, m, d;
  cc_date_of(day, &y, &m, &d);
  snprintf(buf, len, "%s %d %s", poster_wday(day), d, months[m - 1]);
}
