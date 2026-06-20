// PebbleKit JS entry point. Hosts the Clay config UI and translates the form
// values into the compact integer messages the watch expects.
var Clay = require('pebble-clay');
var clayConfig = require('./config');
// autoHandleEvents:false -> we transform settings ourselves before sending.
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

function toMinutes(hhmm) {
  var p = String(hhmm || '00:00').split(':');
  var h = parseInt(p[0], 10) || 0;
  var m = parseInt(p[1], 10) || 0;
  return h * 60 + m;
}

// checkboxgroup value is a boolean array parallel to its options (Sun..Sat).
function daysMask(arr) {
  var mask = 0;
  if (Array.isArray(arr)) {
    for (var i = 0; i < arr.length && i < 7; i++) {
      if (arr[i] && arr[i] !== 'false') {
        mask |= (1 << i);
      }
    }
  }
  return mask;
}

function toInt(v) {
  var n = parseInt(v, 10);
  return isNaN(n) ? 0 : n;
}

// With convert=false, Clay returns each field wrapped as { value: X }. Unwrap it
// so the transforms below see the bare value (string / array / boolean).
function val(v) {
  return (v && typeof v === 'object' && 'value' in v) ? v.value : v;
}

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) {
    return;  // user cancelled
  }
  var dict = clay.getSettings(e.response, false);  // raw values keyed by messageKey

  var out = {
    ENABLED: val(dict.ENABLED) ? 1 : 0,
    DAYS_MASK: daysMask(val(dict.DAYS_MASK)),
    WINDOW_START: toMinutes(val(dict.WINDOW_START)),
    WINDOW_END: toMinutes(val(dict.WINDOW_END)),
    HALF_HOUR: val(dict.HALF_HOUR) ? 1 : 0,
    VIBE_ENABLED: val(dict.VIBE_ENABLED) ? 1 : 0,
    TONE_ENABLED: val(dict.TONE_ENABLED) ? 1 : 0,
    VIBE_FIRST: toInt(val(dict.VIBE_FIRST)),
    VIBE_HOUR: toInt(val(dict.VIBE_HOUR)),
    VIBE_HALF: toInt(val(dict.VIBE_HALF)),
    VIBE_LAST: toInt(val(dict.VIBE_LAST)),
    CLOCK_STYLE: toInt(val(dict.CLOCK_STYLE)),
  };

  Pebble.sendAppMessage(out, function() {
    console.log('[chimer] settings sent: ' + JSON.stringify(out));
  }, function(err) {
    console.log('[chimer] send failed: ' + JSON.stringify(err));
  });
});
