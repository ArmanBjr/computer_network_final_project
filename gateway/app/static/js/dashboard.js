// ===== FileShareX Admin Dashboard JS =====
const PAGE_START = Date.now();

// ---- WebSocket ----
let ws;
function connectWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  ws = new WebSocket(`${proto}://${location.host}/ws`);

  ws.onopen = () => {
    document.getElementById("ws-dot").classList.add("ok");
    document.getElementById("ws-label").textContent = "Live";
    document.getElementById("server-status").textContent = "Core: connected";
    document.getElementById("server-status").classList.add("ok");
  };

  ws.onmessage = (evt) => {
    try {
      const state = JSON.parse(evt.data);
      renderAll(state);
    } catch (e) {
      console.error("WS parse error", e);
    }
  };

  ws.onclose = () => {
    document.getElementById("ws-dot").classList.remove("ok");
    document.getElementById("ws-label").textContent = "Reconnecting...";
    document.getElementById("server-status").textContent = "Core: disconnected";
    document.getElementById("server-status").classList.remove("ok");
    setTimeout(connectWs, 3000);
  };

  ws.onerror = () => ws.close();
}
connectWs();

// ---- Render all state ----
function renderAll(state) {
  renderUsers(state.online_users || []);
  renderTransfers(state.transfers || []);
  renderVoiceSessions(state.voice_sessions || []);
  updateStats(state);
}

// ---- Uptime ticker ----
setInterval(() => {
  const sec = Math.floor((Date.now() - PAGE_START) / 1000);
  const m = Math.floor(sec / 60);
  const s = sec % 60;
  document.getElementById("stat-uptime").textContent = m > 0 ? `${m}m ${s}s` : `${s}s`;
}, 1000);

// ---- Stats ----
function updateStats(state) {
  document.getElementById("stat-users").textContent = (state.online_users || []).length;
  document.getElementById("stat-transfers").textContent = (state.transfers || []).length;
  document.getElementById("stat-voice").textContent = (state.voice_sessions || []).length;
}

// ---- Online Users ----
function renderUsers(users) {
  const el = document.getElementById("users-list");
  const badge = document.getElementById("user-count");
  badge.textContent = users.length;

  if (users.length === 0) {
    el.innerHTML = '<p class="empty">No users online</p>';
    return;
  }
  el.innerHTML = users.map(u =>
    `<span class="user-chip"><span class="dot"></span>${esc(u)}</span>`
  ).join("");
}

// ---- Voice Sessions ----
function renderVoiceSessions(sessions) {
  const el = document.getElementById("voice-list");
  const badge = document.getElementById("voice-count");
  badge.textContent = sessions.length;

  if (sessions.length === 0) {
    el.innerHTML = '<p class="empty">No active voice calls</p>';
    return;
  }
  el.innerHTML = sessions.map(s => `
    <div class="voice-card">
      <div class="vc-icon"><i class='bx bx-phone-call'></i></div>
      <div class="vc-info">
        <strong>${esc(s.caller)} &harr; ${esc(s.callee)}</strong>
        <div class="vc-meta">Session #${s.session_id} &bull; ${s.frames_relayed} frames relayed</div>
      </div>
    </div>
  `).join("");
}

// ---- Active Transfers ----
function renderTransfers(transfers) {
  const tbody = document.getElementById("transfers-body");
  const noMsg = document.getElementById("no-transfers");
  const badge = document.getElementById("transfer-count");
  badge.textContent = transfers.length;

  if (transfers.length === 0) {
    noMsg.style.display = "";
    tbody.innerHTML = "";
    return;
  }
  noMsg.style.display = "none";
  tbody.innerHTML = transfers.map(t => {
    const pct = t.progress || 0;
    const sizeKB = Math.round(t.file_size / 1024);
    return `<tr>
      <td>${t.transfer_id}</td>
      <td>${esc(t.sender)}</td>
      <td>${esc(t.receiver)}</td>
      <td>${esc(t.filename)} <span style="color:var(--text-muted)">(${sizeKB} KB)</span></td>
      <td>
        <div class="progress-bar">
          <div class="progress-fill" style="width:${pct}%"></div>
          <span class="progress-text">${pct}%</span>
        </div>
      </td>
      <td>${t.speed_kbs || 0} KB/s</td>
      <td><span class="state state-${t.state.toLowerCase()}">${t.state}</span></td>
    </tr>`;
  }).join("");
}

// ---- Throttle Controls ----
const scopeSel = document.getElementById("throttle-scope");
const uidField = document.getElementById("uid-field");
const uidInput = document.getElementById("throttle-uid");
const slider   = document.getElementById("throttle-slider");
const valLabel = document.getElementById("throttle-val");
const statusEl = document.getElementById("throttle-status");

scopeSel.addEventListener("change", () => {
  uidField.style.display = scopeSel.value === "user" ? "" : "none";
});

slider.addEventListener("input", () => {
  const kbs = parseInt(slider.value);
  valLabel.textContent = kbs === 0 ? "Unlimited" : `${kbs} KB/s`;
});

document.getElementById("throttle-btn").addEventListener("click", async () => {
  const kbs = parseInt(slider.value);
  const bps = kbs * 1024;
  const scope = scopeSel.value;
  const userId = scope === "user" ? parseInt(uidInput.value || "0") : 0;
  statusEl.textContent = "Applying...";
  try {
    const res = await fetch("/api/throttle", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({scope, bytes_per_second: bps, user_id: userId})
    });
    const data = await res.json();
    statusEl.textContent = data.ok ? "Applied!" : "Failed";
  } catch (e) {
    statusEl.textContent = "Error";
  }
  setTimeout(() => statusEl.textContent = "", 3000);
});

document.getElementById("throttle-clear").addEventListener("click", async () => {
  slider.value = 0;
  valLabel.textContent = "Unlimited";
  statusEl.textContent = "Clearing...";
  try {
    await fetch("/api/throttle", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({scope: "global", bytes_per_second: 0, user_id: 0})
    });
    statusEl.textContent = "Cleared!";
  } catch (e) {
    statusEl.textContent = "Error";
  }
  setTimeout(() => statusEl.textContent = "", 3000);
});

// ---- Fallback polling (if WS fails) ----
setInterval(async () => {
  if (ws && ws.readyState === WebSocket.OPEN) return;
  try {
    const [usersRes, transfersRes, voiceRes] = await Promise.all([
      fetch("/api/online").then(r => r.json()),
      fetch("/api/transfers").then(r => r.json()),
      fetch("/api/voice-sessions").then(r => r.json()),
    ]);
    renderAll({
      online_users: usersRes.users || [],
      transfers: transfersRes.transfers || [],
      voice_sessions: voiceRes.sessions || [],
    });
  } catch (e) {}
}, 3000);

// ---- Utility ----
function esc(s) {
  const d = document.createElement("div");
  d.textContent = s;
  return d.innerHTML;
}
