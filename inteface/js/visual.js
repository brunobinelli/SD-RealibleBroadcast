// ============================================================
//   VISUAL.JS — Canvas network topology + SVG packet animations
//   Depends on: app.js globals (clients, COLORS)
// ============================================================

const canvas = document.getElementById('netCanvas');
const ctx    = canvas.getContext('2d');
const svg    = document.getElementById('animSvg');
let   animId = 0;

// Returns a {server, clientId, ...} map of {x, y} positions
function getPositions() {
  const w = canvas.width, h = canvas.height;
  const cx = w / 2, cy = h / 2;
  const r  = Math.min(w, h) * 0.31;
  const pos = { server: { x: cx, y: cy } };
  (clients || []).forEach((c, i) => {
    const a = (2 * Math.PI * i / clients.length) - Math.PI / 2;
    pos[c.id] = { x: cx + r * Math.cos(a), y: cy + r * Math.sin(a) };
  });
  return pos;
}

function resizeCanvas() {
  const p = canvas.parentElement;
  canvas.width  = p.clientWidth;
  canvas.height = p.clientHeight;
  svg.setAttribute('width',  canvas.width);
  svg.setAttribute('height', canvas.height);
  drawNetwork();
}

function drawNetwork() {
  if (!canvas.width || !(clients || []).length) return;
  const w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  const dark = document.documentElement.getAttribute('data-theme') !== 'light';
  const pos  = getPositions();

  // Background
  ctx.fillStyle = dark ? '#0f0e0d' : '#f7f6f2';
  ctx.fillRect(0, 0, w, h);

  // Edges (server ↔ client)
  clients.forEach(c => {
    const s = pos.server, p = pos[c.id];
    ctx.beginPath(); ctx.moveTo(s.x, s.y); ctx.lineTo(p.x, p.y);
    ctx.strokeStyle = dark ? 'rgba(255,255,255,.05)' : 'rgba(0,0,0,.07)';
    ctx.lineWidth = 1.5; ctx.setLineDash([5, 6]); ctx.stroke(); ctx.setLineDash([]);
  });

  // Server node
  const sp = pos.server;
  ctx.beginPath(); ctx.arc(sp.x, sp.y, 34, 0, 2 * Math.PI);
  ctx.fillStyle = dark ? '#1a2a35' : '#e0f0f2'; ctx.fill();
  ctx.strokeStyle = '#4f98a3'; ctx.lineWidth = 2; ctx.stroke();
  ctx.fillStyle = '#4f98a3';
  ctx.font = 'bold 10px Inter,sans-serif';
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.fillText('SERVIDOR', sp.x, sp.y);

  // Client nodes
  clients.forEach((c, i) => {
    const p     = pos[c.id];
    const r     = 27;
    const color = COLORS[i % COLORS.length];
    const off   = c.status === 'offline';
    const slow  = c.status === 'slow';

    ctx.beginPath(); ctx.arc(p.x, p.y, r, 0, 2 * Math.PI);
    ctx.fillStyle = off  ? (dark ? '#2a1020' : '#fdeaf5') :
                    slow ? (dark ? '#2a1f0a' : '#fff3e0') :
                           (dark ? color + '28' : color + '18');
    ctx.fill();
    ctx.strokeStyle = off ? '#d163a7' : slow ? '#fdab43' : color;
    ctx.lineWidth = 2; ctx.stroke();

    if (c._busy) {
      ctx.beginPath(); ctx.arc(p.x, p.y, r + 7, 0, 2 * Math.PI);
      ctx.strokeStyle = color + '55'; ctx.lineWidth = 3; ctx.stroke();
    }

    ctx.fillStyle = off ? '#d163a7' : (dark ? '#d0cfcc' : '#28251d');
    ctx.font = 'bold 12px Inter,sans-serif';
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.fillText(c.name, p.x, p.y - 3);

    const sl = off ? 'OFF' : slow ? 'SLOW' : 'ON';
    const sc = off ? '#d163a7' : slow ? '#fdab43' : '#6daa45';
    ctx.fillStyle = sc; ctx.font = '500 9px Inter,sans-serif';
    ctx.fillText(sl, p.x, p.y + 10);

    if (c.lossRate > 0) {
      ctx.fillStyle = '#fdab43';
      ctx.font = "9px 'JetBrains Mono',monospace";
      ctx.fillText(`${Math.round(c.lossRate * 100)}%loss`, p.x, p.y + r + 14);
    }
  });
}

// Animates a packet circle from one node to another along the SVG overlay
function animPacket(from, to, type) {
  const pos = getPositions();
  const fp  = pos[from] || pos.server;
  const tp  = pos[to]   || pos.server;
  if (!fp || !tp) return;

  const color = type === 'ack'   ? '#4f98a3' :
                type === 'lost'  ? '#d163a7' :
                type === 'retry' ? '#fdab43' : '#5591c7';
  const sz    = type === 'ack' ? 6 : 8;
  const dur   = 600;
  const start = performance.now();
  ++animId;

  const circ = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
  circ.setAttribute('r', sz); circ.setAttribute('fill', color);
  const trail = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
  trail.setAttribute('r', sz * 0.55); trail.setAttribute('fill', color);
  trail.setAttribute('opacity', '0.35');
  svg.appendChild(trail); svg.appendChild(circ);

  const lostAt = type === 'lost' ? 0.38 + Math.random() * 0.25 : 2;

  function frame(now) {
    const t  = Math.min((now - start) / dur, 1);
    const e  = t < .5 ? 2 * t * t : -1 + (4 - 2 * t) * t;  // ease in-out quad
    const te = Math.max(0, e - .06);

    if (type === 'lost' && t >= lostAt) {
      const p = t - lostAt;
      circ.setAttribute('r',       sz * (1 + p * 9));
      circ.setAttribute('opacity', Math.max(0, 1 - p * 4));
      trail.setAttribute('opacity', 0);
    } else {
      circ.setAttribute('cx',  fp.x + (tp.x - fp.x) * e);
      circ.setAttribute('cy',  fp.y + (tp.y - fp.y) * e);
      trail.setAttribute('cx', fp.x + (tp.x - fp.x) * te);
      trail.setAttribute('cy', fp.y + (tp.y - fp.y) * te);
    }

    if (t < 1) {
      requestAnimationFrame(frame);
    } else {
      try { svg.removeChild(circ);  } catch (_) {}
      try { svg.removeChild(trail); } catch (_) {}
    }
  }
  requestAnimationFrame(frame);
}

// Briefly flashes a halo around a node
function flashNode(nodeId, color) {
  const pos = getPositions();
  const p   = pos[nodeId] || pos.server;
  if (!p) return;
  const c = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
  c.setAttribute('cx', p.x); c.setAttribute('cy', p.y);
  c.setAttribute('r', '32'); c.setAttribute('fill', color);
  c.setAttribute('opacity', '0.3');
  svg.appendChild(c);
  let op = 0.3;
  const t = setInterval(() => {
    op -= 0.03;
    if (op <= 0) { clearInterval(t); try { svg.removeChild(c); } catch (_) {} }
    else c.setAttribute('opacity', op);
  }, 25);
}