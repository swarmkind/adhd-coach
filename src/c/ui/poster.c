#include "poster.h"
#include "../core/cc_time.h"
#include "../core/cc_types.h"

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

void poster_draw_glyph(GContext *ctx, Glyph g, GPoint centre, GColor color) {
  GPoint o = GPoint(centre.x - 8, centre.y - 8);
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 3);
  switch (g) {
    case G_NONE: break;
    case G_LATER:  // Zz
      line(ctx, o, 1, 3, 8, 3); line(ctx, o, 8, 3, 1, 11); line(ctx, o, 1, 11, 8, 11);
      graphics_context_set_stroke_width(ctx, 2);
      line(ctx, o, 10, 8, 14, 8); line(ctx, o, 14, 8, 10, 13); line(ctx, o, 10, 13, 14, 13);
      break;
    case G_DONE: line(ctx, o, 2, 9, 6, 13); line(ctx, o, 6, 13, 14, 4); break;
    case G_SKIP:  // arrow curving over to the right
      arc(ctx, o, 8, 11, 6, -90, 75);
      line(ctx, o, 14, 8, 14, 13); line(ctx, o, 14, 13, 9, 12);
      break;
    case G_UNDO:  // the same arrow, mirrored
      arc(ctx, o, 8, 11, 6, -75, 90);
      line(ctx, o, 2, 8, 2, 13); line(ctx, o, 2, 13, 7, 12);
      break;
    case G_PLUS: line(ctx, o, 8, 2, 8, 14); line(ctx, o, 2, 8, 14, 8); break;
    case G_MINUS: line(ctx, o, 2, 8, 14, 8); break;
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
      line(ctx, o, 8, 6, 8, 9); line(ctx, o, 8, 9, 10, 11); line(ctx, o, 6, 1, 10, 1);
      break;
    case G_NOTE:  // microphone
      graphics_context_set_stroke_width(ctx, 2);
      graphics_fill_rect(ctx, GRect(o.x + 5, o.y, 7, 10), 3, GCornersAll);
      arc(ctx, o, 8, 6, 6, 90, 270);
      line(ctx, o, 8, 13, 8, 15);
      line(ctx, o, 5, 15, 11, 15);
      break;
    case G_UP: line(ctx, o, 2, 11, 8, 5); line(ctx, o, 8, 5, 14, 11); break;
    case G_DOWN: line(ctx, o, 2, 5, 8, 11); line(ctx, o, 8, 11, 14, 5); break;
    case G_STOP: graphics_fill_rect(ctx, GRect(o.x + 3, o.y + 3, 11, 11), 0, GCornerNone); break;
    case G_ALARM:  // bell
      graphics_context_set_stroke_width(ctx, 2);
      arc(ctx, o, 8, 7, 5, -90, 90);
      line(ctx, o, 3, 7, 3, 12); line(ctx, o, 13, 7, 13, 12); line(ctx, o, 1, 12, 15, 12);
      graphics_fill_rect(ctx, GRect(o.x + 6, o.y + 14, 5, 2), 0, GCornerNone);
      break;
  }
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
  graphics_draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(19, 3, 80, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
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
