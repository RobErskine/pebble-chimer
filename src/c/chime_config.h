// Config model + scheduling math + alert primitives for the Chimer app.
// Include <pebble.h> BEFORE this header. NOTE: the alert primitives below use
// the Vibes and Speaker APIs, which are foreground-only -- they are NOT
// available to a background worker (confirmed: pebble_worker.h omits them),
// which is why the chime engine uses the Wakeup API instead.
#pragma once

#define CONFIG_PERSIST_KEY 1
#define CONFIG_SCHEMA_VERSION 3

typedef enum {
  CHIME_EVENT_FIRST = 0,  // first chime of the active window
  CHIME_EVENT_HOUR  = 1,  // an ordinary top-of-hour chime
  CHIME_EVENT_HALF  = 2,  // a :30 half-hour chime
  CHIME_EVENT_LAST  = 3,  // last chime of the active window
  CHIME_EVENT_COUNT = 4,
} ChimeEvent;

// How the on-screen clock formats the time. CLOCK_MATCH follows the watch's
// own 12h/24h system setting.
typedef enum {
  CLOCK_MATCH = 0,
  CLOCK_12H   = 1,
  CLOCK_24H   = 2,
} ClockStyle;

// Vibration pattern presets, selectable per event from the phone config.
typedef enum {
  VIBE_NONE        = 0,
  VIBE_SHORT       = 1,
  VIBE_DOUBLE      = 2,
  VIBE_LONG        = 3,
  VIBE_LONG_DOUBLE = 4,  // long pulse followed by two short pulses
} VibePreset;

typedef struct {
  uint8_t  schema_version;
  bool     enabled;
  uint8_t  days_mask;     // bit i set => weekday i active (0=Sun .. 6=Sat, tm_wday)
  uint16_t window_start;  // minutes since midnight (inclusive)
  uint16_t window_end;    // minutes since midnight (inclusive)
  bool     half_hour;     // also chime at :30
  uint16_t interval_min;  // reserved for v2 custom interval (unused in MVP)
  bool     vibe_enabled;  // play the vibration pattern (independent of tone)
  bool     tone_enabled;  // also play a speaker tone (if hardware supports it)
  uint8_t  vibe[CHIME_EVENT_COUNT];  // VibePreset per ChimeEvent
  uint8_t  clock_style;   // ClockStyle for the on-screen clock
} ChimeConfig;

static inline ChimeConfig chime_config_default(void) {
  ChimeConfig c = {
    .schema_version = CONFIG_SCHEMA_VERSION,
    .enabled = true,
    .days_mask = 0x3E,        // Mon..Fri (bits 1-5); Sun/Sat off
    .window_start = 7 * 60,   // 07:00
    .window_end = 23 * 60,    // 23:00
    .half_hour = false,
    .interval_min = 60,
    .vibe_enabled = true,
    .tone_enabled = false,
    .vibe = { VIBE_LONG_DOUBLE, VIBE_DOUBLE, VIBE_SHORT, VIBE_LONG },
    .clock_style = CLOCK_MATCH,
  };
  return c;
}

static inline ChimeConfig chime_config_load(void) {
  ChimeConfig c;
  if (persist_exists(CONFIG_PERSIST_KEY) &&
      persist_get_size(CONFIG_PERSIST_KEY) == (int)sizeof(ChimeConfig)) {
    persist_read_data(CONFIG_PERSIST_KEY, &c, sizeof(c));
    if (c.schema_version == CONFIG_SCHEMA_VERSION) {
      return c;
    }
  }
  return chime_config_default();
}

static inline void chime_config_save(const ChimeConfig *c) {
  persist_write_data(CONFIG_PERSIST_KEY, c, sizeof(*c));
}

// ---- Scheduling helpers (pure arithmetic; safe in app or worker) ----

// First valid chime minute-of-day at or after window_start.
static inline int chime_first_minute(const ChimeConfig *c) {
  int r = c->window_start % 60;
  if (c->half_hour) {
    if (r == 0)  return c->window_start;
    if (r <= 30) return c->window_start - r + 30;
    return c->window_start - r + 60;
  }
  return (r == 0) ? c->window_start : c->window_start - r + 60;
}

// Last valid chime minute-of-day at or before window_end.
static inline int chime_last_minute(const ChimeConfig *c) {
  int r = c->window_end % 60;
  if (c->half_hour && r >= 30) {
    return c->window_end - r + 30;
  }
  return c->window_end - r;  // the :00 at or before window_end
}

// Decide whether the given local time is a chime moment, and which event.
// Checks only the active window + minute pattern -- caller must check enabled
// and the day-of-week mask.
static inline bool chime_classify(const ChimeConfig *c, int hour, int min,
                                  ChimeEvent *out) {
  int mins = hour * 60 + min;
  if (mins < c->window_start || mins > c->window_end) return false;

  int first = chime_first_minute(c);
  int last = chime_last_minute(c);

  if (min == 0) {
    if (mins == first)      *out = CHIME_EVENT_FIRST;
    else if (mins == last)  *out = CHIME_EVENT_LAST;
    else                    *out = CHIME_EVENT_HOUR;
    return true;
  }
  if (min == 30 && c->half_hour) {
    if (mins == last)       *out = CHIME_EVENT_LAST;
    else if (mins == first) *out = CHIME_EVENT_FIRST;
    else                    *out = CHIME_EVENT_HALF;
    return true;
  }
  return false;
}

// ---- Alert primitives (require the SDK header to be included first) ----

static inline void chime_do_vibe(VibePreset p) {
  switch (p) {
    case VIBE_NONE:
      break;
    case VIBE_SHORT:
      vibes_short_pulse();
      break;
    case VIBE_DOUBLE:
      vibes_double_pulse();
      break;
    case VIBE_LONG:
      vibes_long_pulse();
      break;
    case VIBE_LONG_DOUBLE: {
      static const uint32_t seg[] = {500, 150, 120, 100, 120};
      VibePattern pat = { .durations = (uint32_t *)seg, .num_segments = 5 };
      vibes_enqueue_custom_pattern(pat);
      break;
    }
  }
}

// Casio F-91W-style beep: a square wave (like a piezo buzzer) up around 4 kHz,
// short and crisp. We hold ONE pitch and instead mirror the chime's vibration
// rhythm in sound -- a short pulse is one chirp, a double pulse is two chirps,
// a long pulse is a chirp held ~2x as long, etc. So the beep pattern matches
// the buzz pattern and the events stay distinguishable by ear.
#if defined(PBL_SPEAKER)
#define CHIME_TONE_FREQ 4000
#define CHIME_TONE_VOL  90

static void chime_beep_cb(void *data) {
  uint32_t dur = (uint32_t)(uintptr_t)data;
  speaker_play_tone(CHIME_TONE_FREQ, dur, CHIME_TONE_VOL, SpeakerWaveformSquare);
}

// Play a beep sequence from an on/off duration list (on,off,on,off,... in ms --
// the same convention as VibePattern). The first chirp sounds immediately; the
// rest are scheduled with app timers so they play in sequence without blocking.
static inline void chime_play_beeps(const uint32_t *durs, int n) {
  speaker_set_volume(CHIME_TONE_VOL);
  uint32_t t = 0;
  for (int i = 0; i < n; i += 2) {
    uint32_t on = durs[i];
    if (i == 0) {
      speaker_play_tone(CHIME_TONE_FREQ, on, CHIME_TONE_VOL, SpeakerWaveformSquare);
    } else {
      app_timer_register(t, chime_beep_cb, (void *)(uintptr_t)on);
    }
    t += on;                          // chirp length
    if (i + 1 < n) t += durs[i + 1];  // gap before next chirp
  }
}
#endif

static inline void chime_do_tone(VibePreset p) {
#if defined(PBL_SPEAKER)
  // Beep timings mirror the vibration presets in chime_do_vibe above.
  static const uint32_t pat_short[]       = {80};
  static const uint32_t pat_double[]      = {80, 110, 80};
  static const uint32_t pat_long[]        = {180};
  static const uint32_t pat_long_double[] = {300, 150, 90, 110, 90};
  switch (p) {
    case VIBE_NONE:                                            break;
    case VIBE_SHORT:       chime_play_beeps(pat_short, 1);       break;
    case VIBE_DOUBLE:      chime_play_beeps(pat_double, 3);      break;
    case VIBE_LONG:        chime_play_beeps(pat_long, 1);        break;
    case VIBE_LONG_DOUBLE: chime_play_beeps(pat_long_double, 5); break;
  }
#else
  (void)p;
#endif
}

static inline void chime_fire(const ChimeConfig *c, ChimeEvent e) {
  VibePreset p = (VibePreset)c->vibe[e];
  if (c->vibe_enabled) {
    chime_do_vibe(p);
  }
  if (c->tone_enabled) {
    chime_do_tone(p);
  }
}
