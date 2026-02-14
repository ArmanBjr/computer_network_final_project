// ===== FileShareX Admin Dashboard JS =====
const PAGE_START = Date.now();

// ---- Tab switching ----
function switchTab(btn) {
  document.querySelectorAll(".tab").forEach(t => t.classList.remove("active"));
  document.querySelectorAll(".tab-content").forEach(c => c.classList.remove("active"));
  btn.classList.add("active");
  const target = btn.getAttribute("data-tab");
  document.getElementById(target).classList.add("active");
}

// ---- WebSocket (live data only: online users, transfers, voice) ----
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
      renderLive(state);
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

// ---- Render live (WS) data ----
function renderLive(state) {
  renderOnlineUsers(state.online_users || []);
  renderTransfers(state.transfers || []);
  renderVoiceSessions(state.voice_sessions || []);
  updateLiveStats(state);
}

// ---- Uptime ticker ----
setInterval(() => {
  const sec = Math.floor((Date.now() - PAGE_START) / 1000);
  const m = Math.floor(sec / 60);
  const s = sec % 60;
  document.getElementById("stat-uptime").textContent = m > 0 ? `${m}m ${s}s` : `${s}s`;
}, 1000);

// ---- Live Stats ----
function updateLiveStats(state) {
  document.getElementById("stat-online").textContent = (state.online_users || []).length;
  document.getElementById("stat-transfers").textContent = (state.transfers || []).length;
  document.getElementById("stat-voice").textContent = (state.voice_sessions || []).length;
}

// ===================================================================
//  HTTP Polling for Admin Data (DB) — independent of WebSocket
// ===================================================================

async function fetchAdminData() {
  try {
    const [statsRes, usersRes, filesRes, msgsRes, transfersRes, voiceRes] = await Promise.all([
      fetch("/api/admin/stats").then(r => r.json()).catch(() => ({})),
      fetch("/api/admin/users").then(r => r.json()).catch(() => ({users:[]})),
      fetch("/api/admin/files").then(r => r.json()).catch(() => ({files:[],stats:{}})),
      fetch("/api/admin/messages").then(r => r.json()).catch(() => ({stats:{},recent:[]})),
      fetch("/api/admin/transfers").then(r => r.json()).catch(() => ({transfers:[]})),
      fetch("/api/admin/voice-calls").then(r => r.json()).catch(() => ({calls:[]})),
    ]);

    // System stats
    updateDbStats(statsRes);

    // Message breakdown
    if (msgsRes.stats) updateMessageBreakdown(msgsRes.stats);

    // All users table
    renderAllUsers(usersRes.users || []);

    // All files table + overview quick list
    renderAllFiles(filesRes.files || [], filesRes.stats || {});
    renderOverviewFiles(filesRes.files || []);

    // Recent messages table
    renderRecentMessages(msgsRes.recent || []);

    // Transfer history table
    renderTransferHistory(transfersRes.transfers || []);

    // Voice calls (browser + Core)
    renderVoiceSessions(voiceRes.calls || []);
    setEl("stat-voice", (voiceRes.calls || []).length);
  } catch (e) {
    console.error("Admin data fetch error:", e);
  }
}

// Fetch admin data immediately, then every 5 seconds
fetchAdminData();
setInterval(fetchAdminData, 5000);

// ---- DB Stats ----
function updateDbStats(stats) {
  setEl("stat-total-users", stats.total_users || 0);
  setEl("stat-total-msgs", stats.total_messages || 0);
  setEl("stat-total-files", stats.total_files || 0);
  setEl("stat-storage", formatSize(stats.total_file_bytes || 0));
}

// ---- Message breakdown ----
function updateMessageBreakdown(ms) {
  setEl("bd-text", ms.text_count || 0);
  setEl("bd-voice", ms.voice_count || 0);
  setEl("bd-file", ms.file_count || 0);
}

// ---- Online Users ----
function renderOnlineUsers(users) {
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

// ---- Voice Sessions (browser + Core) ----
function renderVoiceSessions(calls) {
  const el = document.getElementById("voice-list");
  const badge = document.getElementById("voice-count");
  badge.textContent = calls.length;

  if (calls.length === 0) {
    el.innerHTML = '<p class="empty">No active voice calls</p>';
    return;
  }
  el.innerHTML = calls.map(s => {
    const source = s.source === "browser" ? "Browser WebSocket" : "C++ UDP/Opus";
    const sourceColor = s.source === "browser" ? "var(--blue)" : "var(--purple)";
    let meta = "";
    if (s.source === "browser") {
      const dur = s.duration_seconds || 0;
      const m = String(Math.floor(dur / 60)).padStart(2, "0");
      const sec = String(dur % 60).padStart(2, "0");
      meta = `Duration: ${m}:${sec}`;
    } else {
      meta = `Session #${s.session_id || 0} &bull; ${s.frames_relayed || 0} frames`;
    }
    return `
    <div class="voice-card">
      <div class="vc-icon"><i class='bx bx-phone-call'></i></div>
      <div class="vc-info">
        <strong>${esc(s.caller)} &harr; ${esc(s.callee)}</strong>
        <div class="vc-meta">${meta} &bull; <span style="color:${sourceColor}">${source}</span></div>
      </div>
    </div>`;
  }).join("");
}

// ---- Active Transfers (live from Core) ----
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
      <td><span class="state state-${(t.state||'').toLowerCase()}">${t.state || ''}</span></td>
    </tr>`;
  }).join("");
}

// ---- Overview: Recent Files quick list ----
function renderOverviewFiles(files) {
  const el = document.getElementById("overview-files-list");
  const badge = document.getElementById("overview-files-count");
  const recent = files.slice(0, 5); // show last 5
  badge.textContent = files.length;

  if (!recent.length) {
    el.innerHTML = '<p class="empty">No files shared yet</p>';
    return;
  }
  el.innerHTML = recent.map(f => `
    <div class="voice-card">
      <div class="vc-icon" style="background:rgba(59,130,246,.15);color:var(--blue)"><i class='bx bx-file'></i></div>
      <div class="vc-info">
        <strong>${esc(f.filename)}</strong>
        <div class="vc-meta">${esc(f.sender)} &rarr; ${esc(f.receiver)} &bull; ${formatSize(f.size)} &bull; <span class="state state-${statusClass(f.status)}">${f.status}</span></div>
      </div>
    </div>
  `).join("");
}

// ---- All Users (DB) ----
function renderAllUsers(users) {
  const tbody = document.getElementById("all-users-body");
  const noMsg = document.getElementById("no-users-msg");
  setEl("all-users-count", users.length);

  if (!users.length) {
    noMsg.style.display = "";
    tbody.innerHTML = "";
    return;
  }
  noMsg.style.display = "none";
  tbody.innerHTML = users.map(u => `<tr>
    <td>${u.id}</td>
    <td><strong>${esc(u.username)}</strong></td>
    <td class="truncate">${esc(u.email)}</td>
    <td>${dateShort(u.created_at)}</td>
    <td>${u.messages_sent}</td>
    <td>${u.files_sent}</td>
    <td>${u.files_received}</td>
  </tr>`).join("");
}

// ---- All Files (DB) ----
function renderAllFiles(files, stats) {
  const tbody = document.getElementById("all-files-body");
  const noMsg = document.getElementById("no-files-msg");
  setEl("all-files-count", files.length);

  // Stats bar
  const bar = document.getElementById("file-stats-bar");
  if (stats) {
    bar.innerHTML = `
      <span class="file-stat-chip">Total: <strong>${stats.total_files || 0}</strong></span>
      <span class="file-stat-chip">Pending: <strong>${stats.pending || 0}</strong></span>
      <span class="file-stat-chip">Downloaded: <strong>${stats.downloaded || 0}</strong></span>
      <span class="file-stat-chip">Storage: <strong>${formatSize(stats.total_bytes || 0)}</strong></span>
    `;
  }

  if (!files.length) {
    noMsg.style.display = "";
    tbody.innerHTML = "";
    return;
  }
  noMsg.style.display = "none";
  tbody.innerHTML = files.map(f => {
    const routeCol = f.core_transfer_id
      ? `<span class="state state-ok" title="Core TID: ${f.core_transfer_id}">Core #${f.core_transfer_id}</span>`
      : `<span class="state state-warn">Local</span>`;
    const actionCol = f.core_transfer_id
      ? ''
      : `<button class="btn-retry-core" data-fid="${f.id}" data-sender="${esc(f.sender)}" title="Retry via Core">&#x21BB; Retry</button>`;
    return `<tr>
      <td>${f.id}</td>
      <td>${esc(f.sender)}</td>
      <td>${esc(f.receiver)}</td>
      <td class="truncate" title="${esc(f.filename)}">${esc(f.filename)}</td>
      <td>${formatSize(f.size)}</td>
      <td><span class="state state-${statusClass(f.status)}">${f.status}</span></td>
      <td>${routeCol}</td>
      <td>${actionCol}</td>
      <td>${dateShort(f.created_at)}</td>
    </tr>`;
  }).join("");

  // Attach retry click handlers
  tbody.querySelectorAll(".btn-retry-core").forEach(btn => {
    btn.addEventListener("click", async () => {
      const fid = btn.dataset.fid;
      const sender = btn.dataset.sender;
      btn.disabled = true;
      btn.textContent = "Retrying...";
      try {
        const fd = new FormData();
        fd.append("username", sender);
        const resp = await fetch(`/api/retry-upload/${fid}`, {method:"POST", body: fd});
        const data = await resp.json();
        if (data.ok) {
          btn.textContent = "Done!";
          btn.style.background = "#27ae60";
          setTimeout(pollHistorical, 1500);  // refresh table
        } else {
          btn.textContent = "Failed";
          btn.title = data.msg || "Unknown error";
          btn.style.background = "#c0392b";
          setTimeout(() => { btn.textContent = "↻ Retry"; btn.disabled = false; btn.style.background=""; }, 4000);
        }
      } catch(e) {
        btn.textContent = "Error";
        setTimeout(() => { btn.textContent = "↻ Retry"; btn.disabled = false; btn.style.background=""; }, 4000);
      }
    });
  });
}

// ---- Recent Messages (DB) ----
function renderRecentMessages(msgs) {
  const tbody = document.getElementById("recent-msgs-body");
  const noMsg = document.getElementById("no-msgs-msg");
  setEl("recent-msgs-count", msgs.length);

  if (!msgs.length) {
    noMsg.style.display = "";
    tbody.innerHTML = "";
    return;
  }
  noMsg.style.display = "none";
  tbody.innerHTML = msgs.map(m => {
    const typeCls = `type-${m.type}`;
    let contentCol = "";
    if (m.type === "text") {
      contentCol = `<span class="truncate" style="display:inline-block;max-width:200px">${esc(m.content)}</span>`;
    } else if (m.type === "voice") {
      contentCol = `<i class='bx bx-microphone'></i> Voice message`;
    } else if (m.type === "file") {
      contentCol = `<i class='bx bx-file'></i> ${esc(m.file_name || 'file')} (${formatSize(m.file_size || 0)})`;
    }
    return `<tr>
      <td>${m.id}</td>
      <td>${esc(m.sender)}</td>
      <td>${esc(m.receiver)}</td>
      <td><span class="type-badge ${typeCls}">${m.type}</span></td>
      <td>${contentCol}</td>
      <td>${dateShort(m.created_at)}</td>
    </tr>`;
  }).join("");
}

// ---- Transfer History (DB) ----
function renderTransferHistory(transfers) {
  const tbody = document.getElementById("history-transfers-body");
  const noMsg = document.getElementById("no-history-transfers-msg");
  setEl("history-transfers-count", transfers.length);

  if (!transfers.length) {
    noMsg.style.display = "";
    tbody.innerHTML = "";
    return;
  }
  noMsg.style.display = "none";
  tbody.innerHTML = transfers.map(t => `<tr>
    <td>${t.transfer_id}</td>
    <td>${esc(t.sender)}</td>
    <td>${esc(t.receiver)}</td>
    <td class="truncate" title="${esc(t.filename)}">${esc(t.filename)}</td>
    <td>${formatSize(t.file_size)}</td>
    <td>
      <div class="progress-bar">
        <div class="progress-fill" style="width:${t.progress}%"></div>
        <span class="progress-text">${t.progress}%</span>
      </div>
    </td>
    <td><span class="state state-${(t.state||'').toLowerCase()}">${t.state || ''}</span></td>
    <td>${dateShort(t.updated_at || t.created_at)}</td>
  </tr>`).join("");
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

// ---- WS Fallback polling for live data ----
setInterval(async () => {
  if (ws && ws.readyState === WebSocket.OPEN) return;
  try {
    const [usersRes, transfersRes, voiceRes] = await Promise.all([
      fetch("/api/online").then(r => r.json()),
      fetch("/api/transfers").then(r => r.json()),
      fetch("/api/voice-sessions").then(r => r.json()),
    ]);
    renderLive({
      online_users: usersRes.users || [],
      transfers: transfersRes.transfers || [],
      voice_sessions: voiceRes.sessions || [],
    });
  } catch (e) {}
}, 3000);

// ---- Utility ----
function esc(s) {
  if (!s) return "";
  const d = document.createElement("div");
  d.textContent = s;
  return d.innerHTML;
}

function setEl(id, val) {
  const el = document.getElementById(id);
  if (el) el.textContent = val;
}

function formatSize(b) {
  if (!b || b === 0) return "0 B";
  b = Number(b);
  if (b < 1024) return b + " B";
  if (b < 1048576) return (b / 1024).toFixed(1) + " KB";
  if (b < 1073741824) return (b / 1048576).toFixed(1) + " MB";
  return (b / 1073741824).toFixed(2) + " GB";
}

function dateShort(ds) {
  if (!ds) return "";
  try {
    const d = new Date(ds + (ds.includes("Z") || ds.includes("+") ? "" : "Z"));
    return d.toLocaleDateString([], {month:"short", day:"numeric"}) + " " +
           d.toLocaleTimeString([], {hour:"2-digit", minute:"2-digit"});
  } catch(e) { return ds; }
}

function statusClass(status) {
  if (!status) return "";
  const s = status.toLowerCase();
  if (s === "pending") return "offered";
  if (s === "downloaded") return "completed";
  if (s === "expired" || s === "failed") return "failed";
  return s;
}
