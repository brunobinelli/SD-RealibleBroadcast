// ============================================================
//   APP.JS — Shared state, theme, and bootstrap
//
//   Load order in index.html:
//     visual.js → ui.js → backend.js → app.js  (this file)
//
//   All globals defined here are accessible by the previously
//   loaded modules, because their functions only reference the
//   names at call-time, not at parse-time.
// ============================================================

// ── Shared state ─────────────────────────────────────────────
// Read and written by backend.js event handlers;
// read by ui.js renderers and visual.js drawNetwork.

let serverAddr = 'localhost:8765';
let clients    = [];
let messages   = [];
let stats      = { msgs: 0, acks: 0, lost: 0, ret: 0 };
let running    = false;

const COLORS = ['#4f98a3', '#6daa45', '#d163a7', '#fdab43', '#5591c7'];

// ── Theme ────────────────────────────────────────────────────
(function () {
  const root  = document.documentElement;
  let   theme = matchMedia('(prefers-color-scheme:dark)').matches ? 'dark' : 'light';
  root.setAttribute('data-theme', theme);

  document.querySelector('[data-theme-toggle]').addEventListener('click', () => {
    theme = theme === 'dark' ? 'light' : 'dark';
    root.setAttribute('data-theme', theme);
    drawNetwork();   // re-draw with new color palette
  });
})();

// ── Bootstrap ────────────────────────────────────────────────

// Enter key in the message input triggers broadcast
document.getElementById('msg-inp').addEventListener('keydown', e => {
  if (e.key === 'Enter') doBroadcast();
});

// Keep canvas dimensions in sync with its container
const ro = new ResizeObserver(resizeCanvas);
ro.observe(canvas.parentElement);
resizeCanvas();

// Open SSE stream to the C++ backend
connectSSE();