// Clay configuration form, opened from the Pebble phone app.
// Values are read back in index.js and transformed before being sent to the
// watch (days -> bitmask, HH:MM -> minutes since midnight).
var VIBE_OPTIONS = [
  { label: 'None', value: '0' },
  { label: 'Short pulse', value: '1' },
  { label: 'Double pulse', value: '2' },
  { label: 'Long pulse', value: '3' },
  { label: 'Long + 2 short', value: '4' },
];

module.exports = [
  { type: 'heading', defaultValue: 'Chimer' },
  {
    type: 'text',
    defaultValue: 'Hourly chimes that run in the background. Pick the days, ' +
      'the active window, and a vibration for each kind of chime.',
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'General' },
      { type: 'toggle', messageKey: 'ENABLED', label: 'Chimes enabled', defaultValue: true },
      {
        type: 'checkboxgroup',
        messageKey: 'DAYS_MASK',
        label: 'Active days (Sun-Sat)',
        defaultValue: [false, true, true, true, true, true, false],
        options: ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'],
      },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Display' },
      {
        type: 'select', messageKey: 'CLOCK_STYLE', label: 'Clock display',
        defaultValue: '0',
        options: [
          { label: 'Match watch', value: '0' },
          { label: '12-hour (AM/PM)', value: '1' },
          { label: '24-hour', value: '2' },
        ],
      },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Active window' },
      {
        type: 'input', messageKey: 'WINDOW_START', label: 'Start',
        defaultValue: '07:00', attributes: { type: 'time' },
      },
      {
        type: 'input', messageKey: 'WINDOW_END', label: 'End',
        defaultValue: '23:00', attributes: { type: 'time' },
      },
      { type: 'toggle', messageKey: 'HALF_HOUR', label: 'Also chime at :30', defaultValue: false },
    ],
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Alerts' },
      { type: 'toggle', messageKey: 'TONE_ENABLED', label: 'Play tone (speaker)', defaultValue: false },
      { type: 'toggle', messageKey: 'VIBE_ENABLED', label: 'Vibrate', defaultValue: true },
      { type: 'select', messageKey: 'VIBE_FIRST', label: 'First of day', defaultValue: '4', options: VIBE_OPTIONS },
      { type: 'select', messageKey: 'VIBE_HOUR', label: 'Top of hour', defaultValue: '2', options: VIBE_OPTIONS },
      { type: 'select', messageKey: 'VIBE_HALF', label: 'Half hour', defaultValue: '1', options: VIBE_OPTIONS },
      { type: 'select', messageKey: 'VIBE_LAST', label: 'Last of day', defaultValue: '3', options: VIBE_OPTIONS },
    ],
  },
  { type: 'submit', defaultValue: 'Save' },
];
