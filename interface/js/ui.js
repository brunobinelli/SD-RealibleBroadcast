// ============================================================
//   UI.JS — DOM rendering: client cards, ACK grid, message
//           table, stats counter, and event log
//   Depends on: app.js globals (clients, messages, stats, COLORS)
//               visual.js (drawNetwork)
// ============================================================

let logCnt = 0;

// ── Client panel ─────────────────────────────────────────────

function renderClients() {
  const list = document.getElementById('client-list');
  list.innerHTML = '';

  clients.forEach((c, i) => {
    const color  = COLORS[i % COLORS.length];
    const sCls   = c.status === 'online' ? 's-on' : c.status === 'offline' ? 's-off' : 's-slow';
    const sLabel = c.status === 'online' ? 'Online' : c.status === 'offline' ? 'Offline' : 'Lento';
    const lossP  = Math.round((c.lossRate || 0) * 100);

    const div = document.createElement('div');
    div.className = 'cc'; div.id = `cc-${c.id}`;
    div.innerHTML = `
      <div class="cc-head">
        <div class="cc-av" style="background:${color}22;color:${color}">${c.name}</div>
        <div class="cc-name">Processo ${c.name}</div>
        <span class="sbadge ${sCls}" id="sbadge-${c.id}">${sLabel}</span>
      </div>
      <div class="cc-btns">
        <button class="tb${c.status === 'online'  ? ' on' : ''}" onclick="setClientStatus(${c.id},'online')">Online</button>
        <button class="tb${c.status === 'offline' ? ' on' : ''}" onclick="setClientStatus(${c.id},'offline')">Offline</button>
        <button class="tb${c.status === 'slow'    ? ' on' : ''}" onclick="setClientStatus(${c.id},'slow')">Lento</button>
      </div>
      <div class="loss-row">
        <span>Perda:</span>
        <input type="range" min="0" max="100" value="${lossP}"
          oninput="setClientLoss(${c.id},this.value/100);document.getElementById('ll-${c.id}').textContent=this.value+'%'">
        <span id="ll-${c.id}">${lossP}%</span>
      </div>
      <div class="ack-dots" id="dots-${c.id}"></div>`;
    list.appendChild(div);
  });

  renderClientDots();
  drawNetwork();
}

function renderClientDots() {
  clients.forEach(c => {
    const row = document.getElementById(`dots-${c.id}`);
    if (!row) return;
    row.innerHTML = messages.slice(-12).map(m => {
      const ack = m.acks?.[c.id];
      const cls = !ack                          ? 'dot-idle'    :
                  ack.state === 'confirmed'     ? 'dot-ok'      :
                  ack.state === 'failed'        ? 'dot-fail'    :
                  ack.state === 'retrying'      ? 'dot-retry'   :
                  ack.state === 'sending'       ? 'dot-sending' : 'dot-idle';
      return `<span class="dot ${cls}" title="Msg #${m.id}: ${ack?.state || '—'}"></span>`;
    }).join('');
  });
}

// ── ACK grid (messages × clients) ───────────────────────────

function renderAckGrid() {
  const wrap       = document.getElementById('ack-grid');
  const recentMsgs = messages.slice(-8);

  if (!recentMsgs.length) {
    wrap.innerHTML = '<span style="font-size:.75rem;font-family:var(--mono);color:var(--text-faint)">Nenhuma mensagem.</span>';
    return;
  }

  let h = '<div class="ag-hdr">';
  clients.forEach((c, i) => {
    h += `<div class="ag-hc" style="color:${COLORS[i % COLORS.length]}">${c.name}</div>`;
  });
  h += '</div>';

  recentMsgs.forEach(m => {
    h += `<div class="ag-row">
      <div class="ag-rl" title="${m.content}">#${m.id} ${m.content.substring(0, 8)}…</div>`;
    clients.forEach(c => {
      const ack = m.acks?.[c.id];
      let cls = 'ag-idle', lbl = '—';
      if (ack) {
        if      (ack.state === 'confirmed') { cls = 'ag-ok';    lbl = '✓'; }
        else if (ack.state === 'failed')    { cls = 'ag-fail';  lbl = '✗'; }
        else if (ack.state === 'retrying')  { cls = 'ag-retry'; lbl = `↻${ack.retries || ''}`; }
        else if (ack.state === 'sending')   { cls = 'ag-send';  lbl = '⇒'; }
        else                                { cls = 'ag-idle';  lbl = '…'; }
      }
      h += `<div class="ag-cell ${cls}" title="${c.name} — ${ack?.state || 'pending'}">${lbl}</div>`;
    });
    h += '</div>';
  });

  wrap.innerHTML = h;
}

// ── Message table ────────────────────────────────────────────

function renderMsgTable() {
  const tb = document.getElementById('msg-tbody');
  if (!messages.length) {
    tb.innerHTML = '<tr><td colspan="4" style="text-align:center;color:var(--text-faint);padding:.5rem">—</td></tr>';
    return;
  }
  tb.innerHTML = [...messages].reverse().slice(0, 10).map(m => {
    const conf  = clients.filter(c => m.acks?.[c.id]?.state === 'confirmed').length;
    const total = clients.length;
    const p     = m.done ? (m.success ? 'p-ok' : 'p-fail') : 'p-pend';
    const l     = m.done ? (m.success ? '✓ OK' : '✗ Falha') : '…';
    return `<tr>
      <td>#${m.id}</td>
      <td style="max-width:90px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap"
          title="${m.content}">${m.content}</td>
      <td>${conf}/${total}</td>
      <td><span class="pill ${p}">${l}</span></td>
    </tr>`;
  }).join('');
}

// ── Stats bar ────────────────────────────────────────────────

function updateStats() {
  document.getElementById('st-msgs').textContent = stats.msgs;
  document.getElementById('st-acks').textContent = stats.acks;
  document.getElementById('st-lost').textContent = stats.lost;
  document.getElementById('st-ret').textContent  = stats.ret;
}

function setStatus(t) {
  document.getElementById('status-txt').textContent = t;
}

// ── Event log ────────────────────────────────────────────────

const LOG_COLORS = {
  send:  '#5591c7', recv: '#6daa45', ack_s: '#4f98a3', ack_r: '#4f98a3',
  lost:  '#d163a7', retry: '#fdab43', ok:   '#6daa45', fail:  '#d163a7',
  info:  '#797876', state: '#797876',
};

function addLog(type, tag, msg) {
  logCnt++;
  document.getElementById('log-cnt').textContent = logCnt;

  const wrap  = document.getElementById('log-wrap');
  const el    = document.createElement('div');
  el.className = `le le-${type}`;

  const now = new Date();
  const ts  = [
    now.getHours().toString().padStart(2, '0'),
    now.getMinutes().toString().padStart(2, '0'),
    now.getSeconds().toString().padStart(2, '0'),
  ].join(':') + '.' + now.getMilliseconds().toString().padStart(3, '0');

  const color = LOG_COLORS[type] || '#797876';
  el.innerHTML =
    `<span class="lts">${ts}</span>` +
    `<span class="lt" style="background:${color}22;color:${color}">${tag}</span>` +
    msg;

  wrap.appendChild(el);
  wrap.scrollTop = wrap.scrollHeight;
  while (wrap.children.length > 250) wrap.removeChild(wrap.firstChild);
}

function clearLog() {
  document.getElementById('log-wrap').innerHTML = '';
  logCnt = 0;
  document.getElementById('log-cnt').textContent = '0';
}