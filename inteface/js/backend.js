// ============================================================
//   BACKEND.JS — SSE connection to C++ server, algorithm
//                event handlers, and HTTP action functions
//   Depends on: app.js globals (serverAddr, clients, messages,
//               stats, running), ui.js, visual.js
// ============================================================

let sse            = null;
let sseReconnTimer = null;

// ── SSE connection ───────────────────────────────────────────

function connectSSE() {
  if (sse) { sse.close(); sse = null; }
  setConnStatus('connecting');

  sse = new EventSource(`http://${serverAddr}/events`);

  sse.onopen = () => {
    setConnStatus('connected');
    document.getElementById('offline-banner').classList.remove('show');
    addLog('info', 'CONN', 'Conectado ao backend C++ em ' + serverAddr);
    closePortDialog();
  };

  sse.onerror = () => {
    setConnStatus('disconnected');
    document.getElementById('offline-banner').classList.add('show');
    sse.close(); sse = null;
    clearTimeout(sseReconnTimer);
    sseReconnTimer = setTimeout(connectSSE, 3000);
  };

  const handlers = {
    state:      onStateEvent,
    send:       onSendEvent,
    recv:       onRecvEvent,
    ack_s:      onAckSEvent,
    ack_r:      onAckREvent,
    lost:       onLostEvent,
    retry:      onRetryEvent,
    ok:         onOkEvent,
    fail:       onFailEvent,
    info:       onInfoEvent,
    ack_update: onAckUpdate,
  };
  for (const [evt, fn] of Object.entries(handlers))
    sse.addEventListener(evt, e => fn(JSON.parse(e.data)));
}

function setConnStatus(s) {
  const badge = document.getElementById('conn-badge');
  const text  = document.getElementById('conn-text');
  badge.className  = 'conn-badge ' + s;
  text.textContent = s === 'connected'  ? 'conectado'   :
                     s === 'connecting' ? 'conectando…' : 'desconectado';
}

// ── Algorithm event handlers ─────────────────────────────────

function onStateEvent(d) {
  clients  = d.clients  || [];
  messages = d.messages || [];
  if (d.config) {
    document.getElementById('cfg-timeout').value = d.config.ackTimeout;
    document.getElementById('cfg-retries').value = d.config.maxRetries;
    document.getElementById('cfg-delay').value   = d.config.simDelay;
  }
  renderClients();
  renderAckGrid();
  renderMsgTable();
  drawNetwork();
}

function onSendEvent(d) {
  stats.msgs++;
  updateStats();
  if (!messages.find(m => m.id === d.msgId)) {
    messages.push({ id: d.msgId, content: d.content, done: false, success: false, acks: {} });
    clients.forEach(c => {
      messages[messages.length - 1].acks[c.id] = { state: 'pending', retries: 0 };
    });
  }
  setStatus(`Difundindo msg #${d.msgId}: "${d.content}"…`);
  addLog('send', 'SEND', `Servidor difunde msg #${d.msgId}: "${d.content}" → ${d.total} clientes`);
  flashNode('server', '#5591c7');
  renderAckGrid();
  renderMsgTable();
  running = true;
  document.getElementById('btn-send').disabled = true;
}

function onRecvEvent(d) {
  addLog('recv', 'RECV', `${d.clientName} recebeu msg #${d.msgId}`);
  flashNode(d.clientId, '#6daa45');
  animPacket('server', d.clientId, 'msg');
}

function onAckSEvent(d) {
  addLog('ack_s', 'ACK→', `${d.clientName} → ACK para msg #${d.msgId}`);
  animPacket(d.clientId, 'server', 'ack');
}

function onAckREvent(d) {
  stats.acks++;
  updateStats();
  addLog('ack_r', '←ACK', `Servidor recebeu ACK de ${d.clientName} (msg #${d.msgId})`);
  flashNode('server', '#4f98a3');
}

function onLostEvent(d) {
  stats.lost++;
  updateStats();
  addLog('lost', 'LOST', d.reason || `Perda para cliente ${d.clientName} (msg #${d.msgId})`);
  animPacket('server', d.clientId, 'lost');
}

function onRetryEvent(d) {
  stats.ret++;
  updateStats();
  addLog('retry', 'RTRY', `Retransmissão #${d.attempt} → ${d.clientName} (msg #${d.msgId})`);
  animPacket('server', d.clientId, 'retry');
}

function onOkEvent(d) {
  running = false;
  document.getElementById('btn-send').disabled = false;
  const msg = messages.find(m => m.id === d.msgId);
  if (msg) { msg.done = true; msg.success = true; }
  setStatus(`✓ Msg #${d.msgId} — ENTREGA CONFIÁVEL (${d.confirmed}/${d.confirmed} ACKs)`);
  addLog('ok', ' OK ', `✓ ENTREGA CONFIÁVEL CONFIRMADA — msg #${d.msgId} — ${d.confirmed}/${d.confirmed + d.failed} ACKs`);
  flashNode('server', '#6daa45');
  renderMsgTable();
}

function onFailEvent(d) {
  if (d.clientId == null) {
    // Final broadcast failure
    running = false;
    document.getElementById('btn-send').disabled = false;
    const msg = messages.find(m => m.id === d.msgId);
    if (msg) { msg.done = true; msg.success = false; }
    setStatus(`⚠ Msg #${d.msgId} — PARCIAL (${d.confirmed}/${(d.confirmed || 0) + (d.failed || 0)} ACKs)`);
    addLog('fail', 'FAIL', `✗ ENTREGA PARCIAL msg #${d.msgId} — ${d.confirmed || '?'} ok, ${d.failed || '?'} falha(s)`);
    flashNode('server', '#d163a7');
    renderMsgTable();
  } else {
    // Per-client failure
    addLog('fail', 'FAIL', `Falha definitiva em ${d.clientName} (msg #${d.msgId})`);
  }
}

function onInfoEvent(d) {
  addLog('info', 'INFO', d.msg || JSON.stringify(d));
  if (d.clientId !== undefined) {
    const c = clients.find(x => x.id === d.clientId);
    if (c) renderClients();
  }
}

function onAckUpdate(d) {
  const msg = messages.find(m => m.id === d.msgId);
  if (msg) {
    if (!msg.acks) msg.acks = {};
    msg.acks[d.clientId] = { state: d.state, retries: d.retries };
  }
  renderAckGrid();
  renderClientDots();
}

// ── HTTP actions ─────────────────────────────────────────────

async function doBroadcast() {
  if (running) return;
  const msg = document.getElementById('msg-inp').value.trim();
  if (!msg) return;
  try {
    await fetch(`http://${serverAddr}/broadcast`, {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify({ message: msg }),
    });
  } catch (e) {
    addLog('fail', 'ERR', 'Não foi possível contatar o backend: ' + e.message);
  }
}

async function setClientStatus(id, status) {
  const c    = clients.find(x => x.id === id);
  const loss = c ? (c.lossRate || 0) : 0;
  try {
    await fetch(`http://${serverAddr}/client`, {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify({ id, status, lossRate: loss }),
    });
  } catch (e) {
    addLog('fail', 'ERR', 'Erro: ' + e.message);
  }
}

async function setClientLoss(id, rate) {
  const c      = clients.find(x => x.id === id);
  const status = c ? c.status : 'online';
  try {
    await fetch(`http://${serverAddr}/client`, {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify({ id, status, lossRate: rate }),
    });
  } catch (_) {}
}

async function pushConfig() {
  try {
    await fetch(`http://${serverAddr}/config`, {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        maxRetries: parseInt(document.getElementById('cfg-retries').value),
        ackTimeout: parseInt(document.getElementById('cfg-timeout').value),
        simDelay:   parseInt(document.getElementById('cfg-delay').value),
      }),
    });
  } catch (_) {}
}

async function resetClients() {
  for (const c of clients) {
    await fetch(`http://${serverAddr}/client`, {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify({ id: c.id, status: 'online', lossRate: 0 }),
    }).catch(() => {});
  }
}

// ── Port / address dialog ────────────────────────────────────

function showPortDialog() {
  document.getElementById('srv-addr').value = serverAddr;
  document.getElementById('port-dialog').style.display = 'flex';
}

function closePortDialog() {
  document.getElementById('port-dialog').style.display = 'none';
}

function applyAddr() {
  serverAddr = document.getElementById('srv-addr').value.trim() || 'localhost:8765';
  document.getElementById('srv-url').textContent = serverAddr;
  connectSSE();
}