# Chimer

A configurable hourly-chime app for all Pebble watches — the Casio F-91W
hourly beep, reimagined for your wrist.

At the top of every hour Chimer gives you a vibration, an authentic square-wave
"piezo" beep (on speaker-equipped models), or both, and flashes up a big,
glanceable 7-segment clock before getting out of the way.

## Features

- Hourly chimes, with optional chimes on the half-hour (:30)
- Active days (e.g. weekdays only) and an active window (e.g. 7am–11pm)
- Independent vibration and speaker tone — use either, both, or neither
- Casio-style beep whose rhythm mirrors the vibration pattern
- Four distinct alert patterns: first-of-day, top-of-hour, half-hour, last-of-day
- Large 7-segment clock display; 12-hour, 24-hour, or match the watch
- Configured from the phone via Clay

## How it works

Chimer doesn't run continuously in the background (which would drain the
battery). It uses the Pebble **Wakeup API** to schedule one wakeup at a time:
when it fires, the app launches to the foreground, alerts, shows the time,
schedules the next chime, and closes. The schedule survives a reboot. This is
necessary because vibration and speaker are foreground-only on this hardware.

## Build & install

Requires the [Pebble SDK](https://github.com/coredevices/pebble-tool).

```bash
pebble build
pebble install --phone <YOUR_PHONE_IP>                # to a real watch
pebble install --emulator <platform>                  # aplite / basalt / chalk / diorite / emery
```

The build output is `build/chimer.pbw`.

## Configuration

Open Chimer's settings from the Pebble phone app to set the active days, the
active window, the 12/24-hour clock style, and the per-event vibration and tone
options.

## Project layout

| Path | Purpose |
|------|---------|
| `src/c/main.c` | App logic, scheduling, and the clock UI |
| `src/c/chime_config.h` | Config model, persistence, and alert primitives |
| `src/pkjs/config.js` | Clay settings form |
| `src/pkjs/index.js` | Phone-side bridge: transforms settings → watch messages |

Supports all 5 classic Pebble platforms (aplite, basalt, chalk, diorite, emery).
On non-speaker models, chimes fall back to vibration only.
