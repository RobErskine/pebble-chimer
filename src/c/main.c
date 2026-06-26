// Chimer -- configurable hourly/interval chime app for Pebble Time 2.
//
// Engine: the Wakeup API. We schedule ONE wakeup for the next chime moment.
// When it fires the OS launches this app to the foreground; we play the alert
// (vibration + optional tone -- both are foreground-only on this hardware),
// schedule the next wakeup, then auto-close back to the watchface. Wakeups
// survive reboot, so the chain is self-perpetuating.
//
// Whenever the app is on screen -- the brief flash after a chime, or opened
// normally from the menu -- it shows a big, glanceable clock with the date, the
// next chime, and a one-line settings summary below. The buttons preview each
// chime, and the app (re)arms the next wakeup. Settings arrive from the phone
// (Clay) over AppMessage and trigger a reschedule.
#include <pebble.h>
#include "chime_config.h"

#define WAKEUP_AUTO_EXIT_MS 2000  // how long to stay open after a wakeup alert

#define CLOCK_BORDER 10  // px of empty margin around the big time
#define RND_INSET PBL_IF_ROUND_ELSE(18, 0)  // extra horizontal inset on round screens

static Window *s_window;
static Layer *s_clock_layer;   // custom 7-segment time, drawn to fill the width
static TextLayer *s_meta_layer;
static int s_digits[4];        // current HH:MM digits; -1 = blank (12h leading)
static char s_meta[160];
static bool s_launched_by_wakeup = false;

// ---- Scheduling ----

// Find the next chime moment strictly after `now` (and far enough out to be
// schedulable). Returns false if chimes are disabled or none upcoming.
static bool next_chime(const ChimeConfig *c, time_t now, time_t *out_ts,
                       ChimeEvent *out_evt) {
  if (!c->enabled) return false;

  struct tm tmnow = *localtime(&now);
  int wday = tmnow.tm_wday;
  int mins_now = tmnow.tm_hour * 60 + tmnow.tm_min;

  int step = c->half_hour ? 30 : 60;
  int start = chime_first_minute(c);
  int last = chime_last_minute(c);

  for (int d = 0; d < 8; d++) {
    int w = (wday + d) % 7;
    if (!(c->days_mask & (1 << w))) continue;
    for (int m = start; m <= last; m += step) {
      if (d == 0 && m <= mins_now) continue;  // already passed today

      struct tm t = tmnow;
      t.tm_mday += d;
      t.tm_hour = m / 60;
      t.tm_min = m % 60;
      t.tm_sec = 0;
      time_t ts = mktime(&t);

      // Wakeups can't be scheduled within ~30s of now; skip too-soon slots.
      if (ts < now + 35) continue;

      ChimeEvent e = CHIME_EVENT_HOUR;
      chime_classify(c, m / 60, m % 60, &e);
      *out_ts = ts;
      *out_evt = e;
      return true;
    }
  }
  return false;
}

// Cancel any pending wakeup and schedule the next chime.
static void reschedule(void) {
  wakeup_cancel_all();
  ChimeConfig c = chime_config_load();
  time_t now = time(NULL);
  time_t ts;
  ChimeEvent e;
  if (next_chime(&c, now, &ts, &e)) {
    WakeupId id = wakeup_schedule(ts, (int32_t)e, true);
    APP_LOG(APP_LOG_LEVEL_INFO, "[chimer] scheduled id=%ld evt=%d in %lds",
            (long)id, (int)e, (long)(ts - now));
  } else {
    APP_LOG(APP_LOG_LEVEL_INFO, "[chimer] nothing to schedule");
  }
}

// ---- Clock UI ----

static void days_summary(uint8_t mask, char *buf) {
  static const char letters[] = "SMTWTFS";
  for (int i = 0; i < 7; i++) {
    buf[i] = (mask & (1 << i)) ? letters[i] : '-';
  }
  buf[7] = '\0';
}

static bool use_24h(const ChimeConfig *c) {
  switch (c->clock_style) {
    case CLOCK_12H: return false;
    case CLOCK_24H: return true;
    default:        return clock_is_24h_style();  // CLOCK_MATCH
  }
}

// Which of the 7 segments light up for each digit. Bit order (MSB..LSB):
// a b c d e f g  (a=top, b=top-right, c=bottom-right, d=bottom, e=bottom-left,
// f=top-left, g=middle).
static const uint8_t SEG[10] = {
  0x7E, 0x30, 0x6D, 0x79, 0x33, 0x5B, 0x5F, 0x70, 0x7F, 0x7B,
};

// Draw one 7-segment digit in cell (x,y) of size (dw,dh) with stroke t.
static void draw_digit(GContext *ctx, int val, int x, int y, int dw, int dh, int t) {
  if (val < 0 || val > 9) return;  // blank (e.g. 12h leading hour)
  uint8_t s = SEG[val];
  int h2 = dh / 2;
  if (s & 0x40) graphics_fill_rect(ctx, GRect(x, y, dw, t), 0, GCornerNone);                    // a
  if (s & 0x20) graphics_fill_rect(ctx, GRect(x + dw - t, y, t, h2), 0, GCornerNone);           // b
  if (s & 0x10) graphics_fill_rect(ctx, GRect(x + dw - t, y + h2, t, dh - h2), 0, GCornerNone); // c
  if (s & 0x08) graphics_fill_rect(ctx, GRect(x, y + dh - t, dw, t), 0, GCornerNone);           // d
  if (s & 0x04) graphics_fill_rect(ctx, GRect(x, y + h2, t, dh - h2), 0, GCornerNone);          // e
  if (s & 0x02) graphics_fill_rect(ctx, GRect(x, y, t, h2), 0, GCornerNone);                    // f
  if (s & 0x01) graphics_fill_rect(ctx, GRect(x, y + (dh - t) / 2, dw, t), 0, GCornerNone);     // g
}

// Render HH:MM as large 7-segment digits filling the layer width, leaving a
// CLOCK_BORDER margin. Layout: 4 digits + a colon + 4 gaps span the width:
//   4*dw + colon(0.35*dw) + 4*gap(0.22*dw) = 5.23*dw = available width.
static void clock_update_proc(Layer *layer, GContext *ctx) {
  GRect r = layer_get_bounds(layer);
  // On round screens, shrink the digit block away from the curved left/right edges.
  int avail_w = r.size.w - 2 * CLOCK_BORDER - 2 * RND_INSET;
  int dw = avail_w * 100 / 523;
  int gap = dw * 22 / 100;
  int colon_w = dw * 35 / 100;
  int t = dw * 30 / 100;  // segment stroke thickness (boldness)
  if (t < 2) t = 2;
  int dh = dw * 23 / 10;  // ~2.3 aspect, tall LCD digits
  int maxh = r.size.h - 2 * CLOCK_BORDER;
  if (dh > maxh) dh = maxh;
  int y = CLOCK_BORDER + (r.size.h - 2 * CLOCK_BORDER - dh) / 2;

  graphics_context_set_fill_color(ctx, GColorWhite);

  // Center on the digits actually shown: a blank leading hour (12h, e.g. "9:23")
  // drops one digit cell + one gap so the visible time stays centered. Digit
  // size stays constant so the clock doesn't resize between 9:59 and 10:00.
  bool lead = (s_digits[0] >= 0);
  int vis = lead ? 4 : 3;
  int block_w = vis * dw + vis * gap + colon_w;
  int x = (r.size.w - block_w) / 2;

  if (lead) { draw_digit(ctx, s_digits[0], x, y, dw, dh, t); x += dw + gap; }
  draw_digit(ctx, s_digits[1], x, y, dw, dh, t); x += dw + gap;

  int cx = x + (colon_w - t) / 2;
  int dot = t + 2;
  graphics_fill_rect(ctx, GRect(cx, y + dh / 3 - dot / 2, dot, dot), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(cx, y + 2 * dh / 3 - dot / 2, dot, dot), 0, GCornerNone);
  x += colon_w + gap;

  draw_digit(ctx, s_digits[2], x, y, dw, dh, t); x += dw + gap;
  draw_digit(ctx, s_digits[3], x, y, dw, dh, t);
}

// Format minutes-since-midnight into a compact string in the user's preferred
// style: 24h -> "7:00" / "23:30"; 12h -> "7am" / "11pm" / "7:30am".
static void fmt_clock(int minutes, bool h24, char *buf, size_t n) {
  int h = minutes / 60, m = minutes % 60;
  if (h24) {
    snprintf(buf, n, "%d:%02d", h, m);
  } else {
    int hh = h % 12;
    if (hh == 0) hh = 12;
    const char *ap = (h < 12) ? "am" : "pm";
    if (m == 0) snprintf(buf, n, "%d%s", hh, ap);
    else        snprintf(buf, n, "%d:%02d%s", hh, m, ap);
  }
}

// Pretty label for the active-days bitmask, with common cases named.
static void days_label(uint8_t mask, char *buf, size_t n) {
  switch (mask & 0x7F) {
    case 0x7F: snprintf(buf, n, "Every day"); break;  // all 7
    case 0x3E: snprintf(buf, n, "Mon-Fri");   break;  // weekdays
    case 0x41: snprintf(buf, n, "Weekends");  break;  // Sun + Sat
    default:   days_summary(mask, buf);        break;  // e.g. SMTWTFS w/ dashes
  }
}

static void update_display(void) {
  ChimeConfig c = chime_config_load();
  bool h24 = use_24h(&c);
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);

  // Big time digits. Build the date string before any further localtime() call
  // clobbers lt.
  const char *ampm = "";
  if (h24) {
    s_digits[0] = lt->tm_hour / 10;
    s_digits[1] = lt->tm_hour % 10;
  } else {
    int h = lt->tm_hour % 12;
    if (h == 0) h = 12;
    s_digits[0] = (h >= 10) ? 1 : -1;  // blank the leading zero in 12h
    s_digits[1] = h % 10;
    ampm = (lt->tm_hour < 12) ? "AM" : "PM";
  }
  s_digits[2] = lt->tm_min / 10;
  s_digits[3] = lt->tm_min % 10;

  char date[16];
  strftime(date, sizeof(date), "%a %b %d", lt);

  char days[12];
  days_label(c.days_mask, days, sizeof(days));

  char wstart[12], wend[12];
  fmt_clock(c.window_start, h24, wstart, sizeof(wstart));
  fmt_clock(c.window_end, h24, wend, sizeof(wend));

  char next[28];
  time_t ts;
  ChimeEvent e;
  if (next_chime(&c, now, &ts, &e)) {
    struct tm *nt = localtime(&ts);
    static const char *names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (h24) {
      snprintf(next, sizeof(next), "Next %s %02d:%02d",
               names[nt->tm_wday], nt->tm_hour, nt->tm_min);
    } else {
      int h = nt->tm_hour % 12;
      if (h == 0) h = 12;
      snprintf(next, sizeof(next), "Next %s %d:%02d%s",
               names[nt->tm_wday], h, nt->tm_min, nt->tm_hour < 12 ? "am" : "pm");
    }
  } else {
    snprintf(next, sizeof(next), "Chimes %s", c.enabled ? "--" : "off");
  }

  // Round screens: narrow text area due to curve inset — show date + next only.
  // emery: full 4-line footer with button hint.
  // Other rect (144×168): 3 lines, no button hint.
#if defined(PBL_ROUND)
  snprintf(s_meta, sizeof(s_meta), "%s%s%s\n%s",
           ampm, ampm[0] ? "  " : "", date, next);
#elif defined(PBL_PLATFORM_EMERY)
  snprintf(s_meta, sizeof(s_meta),
           "%s%s%s\n%s\n%s  %s - %s%s\nSEL/UP/DN preview",
           ampm, ampm[0] ? "  " : "", date, next,
           days, wstart, wend, c.half_hour ? "  +:30" : "");
#else
  snprintf(s_meta, sizeof(s_meta), "%s%s%s\n%s\n%s  %s - %s%s",
           ampm, ampm[0] ? "  " : "", date, next,
           days, wstart, wend, c.half_hour ? "  +:30" : "");
#endif

  if (s_clock_layer) layer_mark_dirty(s_clock_layer);
  if (s_meta_layer) text_layer_set_text(s_meta_layer, s_meta);
}

// ---- Events ----

static void inbox_received(DictionaryIterator *it, void *ctx) {
  ChimeConfig c = chime_config_load();
  Tuple *t;
  if ((t = dict_find(it, MESSAGE_KEY_ENABLED)))      c.enabled = t->value->int32 != 0;
  if ((t = dict_find(it, MESSAGE_KEY_DAYS_MASK)))    c.days_mask = (uint8_t)t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_WINDOW_START))) c.window_start = (uint16_t)t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_WINDOW_END)))   c.window_end = (uint16_t)t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_HALF_HOUR)))    c.half_hour = t->value->int32 != 0;
  if ((t = dict_find(it, MESSAGE_KEY_VIBE_ENABLED))) c.vibe_enabled = t->value->int32 != 0;
  if ((t = dict_find(it, MESSAGE_KEY_TONE_ENABLED))) c.tone_enabled = t->value->int32 != 0;
  if ((t = dict_find(it, MESSAGE_KEY_VIBE_FIRST)))   c.vibe[CHIME_EVENT_FIRST] = (uint8_t)t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_VIBE_HOUR)))    c.vibe[CHIME_EVENT_HOUR]  = (uint8_t)t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_VIBE_HALF)))    c.vibe[CHIME_EVENT_HALF]  = (uint8_t)t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_VIBE_LAST)))    c.vibe[CHIME_EVENT_LAST]  = (uint8_t)t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_CLOCK_STYLE)))  c.clock_style = (uint8_t)t->value->int32;
  c.schema_version = CONFIG_SCHEMA_VERSION;
  chime_config_save(&c);
  APP_LOG(APP_LOG_LEVEL_INFO, "[chimer] config saved (enabled=%d days=0x%02x)",
          c.enabled, c.days_mask);
  reschedule();
  update_display();
}

// A wakeup firing while the app happens to be open.
static void wakeup_handler(WakeupId id, int32_t cookie) {
  ChimeConfig c = chime_config_load();
  chime_fire(&c, (ChimeEvent)cookie);
  reschedule();
  update_display();
}

static void tick_handler(struct tm *tick_time, TimeUnits units) {
  update_display();  // keep the clock current while the app is on screen
}

static void test_event(ChimeEvent e) {
  ChimeConfig c = chime_config_load();
  chime_fire(&c, e);
}

static void select_click(ClickRecognizerRef r, void *ctx) { test_event(CHIME_EVENT_HOUR); }
static void up_click(ClickRecognizerRef r, void *ctx)     { test_event(CHIME_EVENT_FIRST); }
static void down_click(ClickRecognizerRef r, void *ctx)   { test_event(CHIME_EVENT_HALF); }

static void click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
}

static void window_load(Window *window) {
  window_set_background_color(window, GColorBlack);
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  // Footer height: emery gets 96px (full 4-line footer), small rect screens get
  // 64px (3 lines at GOTHIC_14), round screens get ~1/3 of height (2 lines).
#if defined(PBL_ROUND)
  int meta_h = b.size.h / 3;
#elif defined(PBL_PLATFORM_EMERY)
  int meta_h = 96;
#else
  int meta_h = 64;
#endif
  // On round screens push the clock down slightly so tall digits clear the top curve.
  int clock_top = PBL_IF_ROUND_ELSE(10, 0);

  s_clock_layer = layer_create(GRect(0, clock_top, b.size.w, b.size.h - meta_h - clock_top));
  layer_set_update_proc(s_clock_layer, clock_update_proc);
  layer_add_child(root, s_clock_layer);

  // Inset the footer horizontally on round screens; use a smaller font on narrow ones.
  int fi = 4 + RND_INSET;
  s_meta_layer = text_layer_create(GRect(fi, b.size.h - meta_h, b.size.w - 2 * fi, meta_h));
  text_layer_set_background_color(s_meta_layer, GColorClear);
  text_layer_set_text_color(s_meta_layer, GColorWhite);
  text_layer_set_font(s_meta_layer, fonts_get_system_font(
    b.size.w < 180 ? FONT_KEY_GOTHIC_14 : FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_meta_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_meta_layer, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(s_meta_layer));

  update_display();
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  layer_destroy(s_clock_layer);
  text_layer_destroy(s_meta_layer);
  s_clock_layer = NULL;
  s_meta_layer = NULL;
}

static void exit_timer_cb(void *data) {
  window_stack_pop_all(false);  // ends the event loop -> app closes to watchface
}

static void init(void) {
  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 64);
  wakeup_service_subscribe(wakeup_handler);

  // If we were launched by a scheduled wakeup, alert immediately and re-arm.
  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    WakeupId id;
    int32_t cookie = CHIME_EVENT_HOUR;
    if (wakeup_get_launch_event(&id, &cookie)) {
      ChimeConfig c = chime_config_load();
      chime_fire(&c, (ChimeEvent)cookie);
    }
    s_launched_by_wakeup = true;
  }

  // Always (re)arm the next chime -- self-heals a broken chain and applies on
  // first run / after settings changes the user made on the phone.
  reschedule();

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  // After a wakeup alert, dismiss ourselves shortly so the flash is brief.
  if (s_launched_by_wakeup) {
    app_timer_register(WAKEUP_AUTO_EXIT_MS, exit_timer_cb, NULL);
  }
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
