#include <pebble.h>
#include "voice_win.h"
#include "poster.h"
#include "railtap.h"
#include "fittext.h"
#include "../coach.h"
#include "../platform/notes.h"
#include "../platform/voice.h"
#include "../platform/buzz.h"
#include "../platform/clock.h"
#include "../platform/store.h"
#include "../platform/wake.h"
#include "home_win.h"
#include "when_win.h"
#include "../core/cc_time.h"

#define ROW_H 52
#define TEXT_X 12
#define NOTES_MAX 100

enum { M_RENAME, M_DELETE, M_KEEP, M_IN30, M_IN60, M_MORNING, M_TODAY, M_FULL, M_PICK };

// ---- shared ----

static void note_title(const CcNote *n, char *buf, int len) {
  if (n->name[0]) { snprintf(buf, len, "%s", n->name); return; }
  cc_sec local = (cc_sec)n->id + clock_tz_offset();
  char d[16], t[12];
  poster_fmt_date(cc_day_of(local), d, sizeof(d));
  poster_fmt_time(cc_min_of(local), t, sizeof(t));
  snprintf(buf, len, "%s %s", d, t);
}

static void fmt_len(uint16_t s, char *buf, int len) { snprintf(buf, len, "%d:%02d", s / 60, s % 60); }

// ---- one note ----

static Window *s_note_win;
static Layer *s_canvas;
static FitText *s_title;
static TextLayer *s_meta, *s_link, *s_body, *s_why;
static ScrollLayer *s_scroll;
static uint32_t s_note_id;
static CcNote s_note;
static char *s_text;  // the open note's transcript, on the heap: the app image must stay under 64 KB
static char s_meta_buf[48];
static bool s_scrolls;

static void note_draw(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  poster_draw_background(ctx, b, poster_kind_color(POSTER_VOICE));
  RailAction rail[3] = { { G_NONE, NULL }, { G_NONE, NULL }, { G_NONE, NULL } };
  if (s_scrolls) { rail[0] = (RailAction){ G_UP, "up" }; rail[2] = (RailAction){ G_DOWN, "down" }; }
  poster_draw_rail(ctx, b, poster_kind_color(POSTER_VOICE), rail);
}

static TextLayer *text_at(Layer *parent, GRect r, const char *font) {
  TextLayer *t = text_layer_create(r);
  text_layer_set_background_color(t, GColorClear);
  text_layer_set_text_color(t, GColorBlack);
  text_layer_set_font(t, fonts_get_system_font(font));
  layer_add_child(parent, text_layer_get_layer(t));
  return t;
}

static void note_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_canvas = layer_create(b);
  layer_set_update_proc(s_canvas, note_draw);
  layer_add_child(root, s_canvas);

  s_text = malloc(CC_NOTE_TEXT_MAX + 1);
  if (!s_text || !notes_read(s_note_id, &s_note, s_text)) {
    if (s_text) s_text[0] = 0;
    memset(&s_note, 0, sizeof(s_note));
  }
  char title[CC_NAME_MAX + 24];
  note_title(&s_note, title, sizeof(title));
  GFont big = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_title = fittext_create(GRect(TEXT_X, 2, POSTER_TEXT_W, 30), big, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), 1,
                           GColorBlack);
  fittext_set(s_title, title);
  layer_add_child(root, fittext_layer(s_title));

  cc_sec local = (cc_sec)s_note.id + clock_tz_offset();
  char d[16], t[12], l[8];
  poster_fmt_date(cc_day_of(local), d, sizeof(d));
  poster_fmt_time(cc_min_of(local), t, sizeof(t));
  fmt_len(s_note.len_s, l, sizeof(l));
  if (s_note.name[0]) snprintf(s_meta_buf, sizeof(s_meta_buf), "%s · %s · %s", d, t, l);
  else snprintf(s_meta_buf, sizeof(s_meta_buf), "%s recording", l);  // the title is the date and time
  s_meta = text_at(root, GRect(TEXT_X, 32, POSTER_TEXT_W, 22), FONT_KEY_GOTHIC_18);
  text_layer_set_text(s_meta, s_meta_buf);

  int y = 56;
  const CcItem *link = coach_item(s_note.link);
  if (link) {
    s_link = text_at(root, GRect(TEXT_X, 52, POSTER_TEXT_W, 22), FONT_KEY_GOTHIC_18_BOLD);
    text_layer_set_text(s_link, link->name);
    y = 76;
  }

  int cw = b.size.w - POSTER_RAIL_W;
  s_scroll = scroll_layer_create(GRect(0, y, cw, b.size.h - y));
  scroll_layer_set_shadow_hidden(s_scroll, true);
  const char *body = s_note.status == CC_NOTE_OK && s_text ? s_text : "No transcript";
  GFont bf = fonts_get_system_font(s_note.status == CC_NOTE_OK ? FONT_KEY_GOTHIC_24 : FONT_KEY_GOTHIC_24_BOLD);
  GSize sz = graphics_text_layout_get_content_size(body, bf, GRect(0, 0, POSTER_TEXT_W, 4000), GTextOverflowModeWordWrap,
                                                   GTextAlignmentLeft);
  s_body = text_layer_create(GRect(TEXT_X, 0, POSTER_TEXT_W, sz.h + 8));
  text_layer_set_background_color(s_body, GColorClear);
  text_layer_set_text_color(s_body, GColorBlack);
  text_layer_set_font(s_body, bf);
  text_layer_set_text(s_body, body);
  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_body));
  int content = sz.h + 16;
  if (s_note.status != CC_NOTE_OK) {  // why it was missed
    s_why = text_layer_create(GRect(TEXT_X, sz.h + 4, POSTER_TEXT_W, 24));
    text_layer_set_background_color(s_why, GColorClear);
    text_layer_set_text_color(s_why, GColorBlack);
    text_layer_set_font(s_why, fonts_get_system_font(FONT_KEY_GOTHIC_18));
    text_layer_set_text(s_why, voice_reason_text(s_note.reason));
    scroll_layer_add_child(s_scroll, text_layer_get_layer(s_why));
    content += 24;
  }
  scroll_layer_set_content_size(s_scroll, GSize(cw, content));
  s_scrolls = content > b.size.h - y;
  scroll_layer_set_click_config_onto_window(s_scroll, w);  // Up / Down scroll
  layer_add_child(root, scroll_layer_get_layer(s_scroll));
}

static void note_appear(Window *w) { fittext_restart(s_title); }

// Rail taps scroll the transcript a screenful; a swipe on it scrolls through the system's touch navigation.
static void note_page(int dir) {
  if (!s_scroll || !s_scrolls) return;
  GPoint off = scroll_layer_get_content_offset(s_scroll);
  int page = layer_get_bounds(scroll_layer_get_layer(s_scroll)).size.h - 24;
  scroll_layer_set_content_offset(s_scroll, GPoint(0, off.y - dir * page), true);
}
static void note_up(ClickRecognizerRef r, void *ctx) { note_page(-1); }
static void note_down(ClickRecognizerRef r, void *ctx) { note_page(+1); }

static void note_unload(Window *w) {
  fittext_destroy(s_title);
  text_layer_destroy(s_meta);
  if (s_link) text_layer_destroy(s_link);
  text_layer_destroy(s_body);
  if (s_why) text_layer_destroy(s_why);
  scroll_layer_destroy(s_scroll);
  layer_destroy(s_canvas);
  s_link = s_why = NULL;
  free(s_text);
  s_text = NULL;
}

void voice_note_open(uint32_t id) {
  s_note_id = id;
  if (!s_note_win) {
    s_note_win = window_create();
    window_set_window_handlers(s_note_win, (WindowHandlers){ .load = note_load, .appear = note_appear, .unload = note_unload });
    railtap_register(s_note_win, &(RailTaps){ note_up, NULL, note_down, NULL, NULL }, true);
  }
  if (window_stack_contains_window(s_note_win)) window_stack_remove(s_note_win, false);  // reload for this note
  window_stack_push(s_note_win, true);
}

// ---- the log ----

static Window *s_log_win;
static MenuLayer *s_menu;
static CcNote *s_notes;
static int s_n;
static uint32_t s_menu_id;
static int s_choice = -1;
static cc_sec s_menu_now;  // the menu's times are from when it opened

static void reload(void) {
  s_n = s_notes ? notes_list(s_notes, NOTES_MAX) : 0;
  if (s_menu) menu_layer_reload_data(s_menu);
}

static uint16_t num_rows(MenuLayer *m, uint16_t section, void *ctx) { return (uint16_t)(s_n ? s_n : 1); }
static int16_t row_h(MenuLayer *m, MenuIndex *i, void *ctx) { return ROW_H; }

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
  char title[CC_NAME_MAX + 24], sub[64], l[8];
  if (!s_n) {
    poster_draw_row(ctx, cell, poster_kind_color(POSTER_VOICE), false, "No notes yet", "Down on home records one");
    return;
  }
  const CcNote *n = &s_notes[idx->row];
  note_title(n, title, sizeof(title));
  fmt_len(n->len_s, l, sizeof(l));
  const CcItem *link = coach_item(n->link);
  const char *todo = cc_todo_for_note(g_todos, n->id) ? " · to do" : "";
  if (n->status != CC_NOTE_OK) snprintf(sub, sizeof(sub), "missed · %s%s", l, todo);
  else if (link) snprintf(sub, sizeof(sub), "%s · %s%s", l, link->name, todo);
  else snprintf(sub, sizeof(sub), "%s%s", l, todo);
  poster_draw_row(ctx, cell, poster_kind_color(POSTER_VOICE), true, title, sub);  // a voice-colour strip on each row
}

static void select_click(MenuLayer *m, MenuIndex *idx, void *ctx) {
  if (s_n) voice_note_open(s_notes[idx->row].id);
}

static void renamed(const char *text) {
  if (!text) return;  // voice.c has buzzed the failure
  if (notes_rename(s_menu_id, text)) buzz_tick();
  reload();
}

// The note as a to-do: its name, else the transcript's start, else its date.
static void make_todo(uint32_t id, cc_sec at) {
  CcNote n;
  char *text = malloc(CC_NOTE_TEXT_MAX + 1);
  if (!text) return;
  if (notes_read(id, &n, text)) {
    if (n.name[0]) snprintf(text, CC_NOTE_TEXT_MAX + 1, "%s", n.name);
    else if (n.status != CC_NOTE_OK) note_title(&n, text, CC_NOTE_TEXT_MAX + 1);
    if (cc_todo_add(g_todos, id, text, at)) {
      store_save_todos();
      wake_rearm();
      ui_refresh_all();
      buzz_tick();
    }
  }
  free(text);
}

cc_sec voice_todo_at(int which, cc_sec now) {
  if (which == 0) return now + 30 * 60;
  if (which == 1) return now + 60 * 60;
  return cc_todo_morning(now, g_cfg.event_time_min);
}

cc_sec voice_todo_pick_start(cc_sec at, cc_sec now) { return at > now ? at : voice_todo_at(2, now); }

const char *voice_todo_label(int which, cc_sec now) {
  static char label[VOICE_TODO_TIMES][28];
  cc_sec at = voice_todo_at(which, now);
  char t[12];
  poster_fmt_time(cc_min_of(at), t, sizeof(t));
  const char *what = which == 0 ? "In 30 min" : which == 1 ? "In 1 hour"
                     : cc_day_of(at) == cc_day_of(now) ? "This morning" : "Tomorrow";
  snprintf(label[which], sizeof(label[which]), "%s · %s", what, t);
  return label[which];
}

static void picked(cc_sec at) {
  make_todo(s_menu_id, at);
  reload();
}

static void after_menu(void *data) {
  int choice = s_choice;
  s_choice = -1;
  if (choice == M_TODAY) { make_todo(s_menu_id, 0); reload(); }
  else if (choice == M_PICK) {
    const CcTodo *t = cc_todo_for_note(g_todos, s_menu_id);
    when_win_open(voice_todo_pick_start(t ? t->at : 0, s_menu_now), picked);
  }
  else if (choice >= M_IN30 && choice <= M_MORNING) { make_todo(s_menu_id, voice_todo_at(choice - M_IN30, s_menu_now)); reload(); }
  else if (choice == M_RENAME) voice_dictate_text(renamed);
  else if (choice == M_DELETE) { notes_delete(s_menu_id); reload(); }
}

static void menu_perform(ActionMenu *menu, const ActionMenuItem *action, void *ctx) {
  s_choice = (int)action_menu_item_get_action_data(action);
}

static void menu_closed(ActionMenu *menu, const ActionMenuItem *performed, void *ctx) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
  app_timer_register(0, after_menu, NULL);  // dictation opens its own window: not from inside the menu
}

static void select_long(MenuLayer *m, MenuIndex *idx, void *ctx) {
  if (!s_n) return;
  s_menu_id = s_notes[idx->row].id;
  s_choice = -1;
  ActionMenuLevel *root = action_menu_level_create(7), *del = action_menu_level_create(2);
  if (g_todos->n < CC_TODO_MAX || cc_todo_for_note(g_todos, s_menu_id)) {
    s_menu_now = clock_now_local();
    for (int i = 0; i < VOICE_TODO_TIMES; i++)
      action_menu_level_add_action(root, voice_todo_label(i, s_menu_now), menu_perform, (void *)(M_IN30 + i));
    action_menu_level_add_action(root, "Pick day and time", menu_perform, (void *)M_PICK);
    action_menu_level_add_action(root, "Add to Today", menu_perform, (void *)M_TODAY);
  } else {
    action_menu_level_add_action(root, "To-do list full", menu_perform, (void *)M_FULL);
  }
  action_menu_level_add_action(root, "Rename", menu_perform, (void *)M_RENAME);
  action_menu_level_add_action(del, "Delete note", menu_perform, (void *)M_DELETE);
  action_menu_level_add_action(del, "Keep", menu_perform, (void *)M_KEEP);
  action_menu_level_add_child(root, del, "Delete");
  poster_open_menu(root, poster_kind_color(POSTER_VOICE), menu_closed);
}

static void log_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  window_set_background_color(w, GColorBlack);
  s_notes = malloc(sizeof(CcNote) * NOTES_MAX);
  s_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_normal_colors(s_menu, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_menu, GColorBlack, GColorBlack);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = num_rows,
    .get_cell_height = row_h,
    .draw_row = draw_row,
    .select_click = select_click,
    .select_long_click = select_long,
  });
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void log_appear(Window *w) { reload(); }

static void log_unload(Window *w) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
  free(s_notes);
  s_notes = NULL;
  s_n = 0;
}

void voice_log_open(void) {
  if (!s_log_win) {
    s_log_win = window_create();
    window_set_window_handlers(s_log_win, (WindowHandlers){ .load = log_load, .appear = log_appear, .unload = log_unload });
  }
  window_stack_push(s_log_win, true);
}
