// ===== FileShareX Messenger — Text, Voice, File Chat =====

const ME = localStorage.getItem("fsx_username") || "";
if (!ME || !localStorage.getItem("fsx_token")) window.location.href = "/login";
document.getElementById("my-name").textContent = ME;
document.getElementById("my-avatar").textContent = ME.charAt(0).toUpperCase();

let selectedUser = null;
let allUsers = [];
let lastMsgId = 0;         // for polling new messages
let mediaRecorder = null;
let audioChunks = [];
let isRecording = false;

// ===================== USERS =====================
async function fetchUsers() {
  try {
    const r = await fetch("/api/online");
    const d = await r.json();
    allUsers = (d.users || []).filter(u => u !== ME);
    renderUsers(allUsers);
  } catch (e) {}
}

function renderUsers(users) {
  const el = document.getElementById("user-list");
  document.getElementById("user-badge").textContent = users.length;
  if (!users.length) { el.innerHTML = '<div class="empty-msg">No other users online</div>'; return; }
  el.innerHTML = users.map(u => {
    const active = u === selectedUser ? " active" : "";
    return `<div class="u-item${active}" onclick="selectUser('${esc(u)}')">
      <div class="u-av">${u.charAt(0).toUpperCase()}</div>
      <span class="u-name">${esc(u)}</span><span class="dot-sm"></span></div>`;
  }).join("");
}

function filterUsers() {
  const q = document.getElementById("search-input").value.toLowerCase();
  renderUsers(allUsers.filter(u => u.toLowerCase().includes(q)));
}

// ===================== SELECT USER =====================
function selectUser(username) {
  selectedUser = username;
  lastMsgId = 0;
  document.getElementById("placeholder").style.display = "none";
  document.getElementById("chat-panel").style.display = "flex";
  document.getElementById("sel-name").textContent = username;
  document.getElementById("sel-avatar").textContent = username.charAt(0).toUpperCase();
  document.getElementById("chat-messages").innerHTML = '<div class="empty-msg">Loading...</div>';
  renderUsers(allUsers.filter(u => {
    const q = document.getElementById("search-input").value.toLowerCase();
    return q ? u.toLowerCase().includes(q) : true;
  }));
  loadConversation();
}

// ===================== MESSAGES =====================
async function loadConversation() {
  if (!selectedUser) return;
  try {
    const r = await fetch(`/api/messages/${enc(ME)}/${enc(selectedUser)}`);
    const d = await r.json();
    const el = document.getElementById("chat-messages");
    if (!d.messages || !d.messages.length) {
      el.innerHTML = '<div class="empty-msg">No messages yet. Say hi!</div>';
      lastMsgId = 0;
      return;
    }
    el.innerHTML = d.messages.map(renderMessage).join("");
    lastMsgId = d.messages[d.messages.length - 1].id;
    scrollToBottom();
  } catch (e) {
    document.getElementById("chat-messages").innerHTML = '<div class="empty-msg">Error loading messages</div>';
  }
}

async function pollNewMessages() {
  if (!selectedUser || lastMsgId === 0) return;
  try {
    const r = await fetch(`/api/messages/${enc(ME)}/${enc(selectedUser)}?after_id=${lastMsgId}`);
    const d = await r.json();
    if (d.messages && d.messages.length > 0) {
      const el = document.getElementById("chat-messages");
      // Remove "no messages" placeholder if present
      const empty = el.querySelector(".empty-msg");
      if (empty) empty.remove();
      d.messages.forEach(m => {
        el.insertAdjacentHTML("beforeend", renderMessage(m));
      });
      lastMsgId = d.messages[d.messages.length - 1].id;
      scrollToBottom();
    }
  } catch (e) {}
}

function renderMessage(m) {
  const isMe = m.sender === ME;
  const side = isMe ? "me" : "them";
  const time = timeShort(m.created_at);

  if (m.type === "text") {
    return `<div class="msg ${side}">
      <div class="bubble">${escHtml(m.content)}</div>
      <div class="meta">${time}</div>
    </div>`;
  }

  if (m.type === "voice") {
    const url = m.file_id ? `/api/download/${m.file_id}?username=${enc(ME)}` : "";
    return `<div class="msg ${side}">
      <div class="bubble voice-bubble">
        <audio controls preload="none" src="${url}"></audio>
      </div>
      <div class="meta"><i class='bx bx-microphone'></i> Voice &bull; ${time}</div>
    </div>`;
  }

  if (m.type === "file") {
    const url = m.file_id ? `/api/download/${m.file_id}?username=${enc(ME)}` : "#";
    const fname = m.file_name || "file";
    const fsize = formatSize(m.file_size || 0);
    return `<div class="msg ${side}">
      <div class="bubble file-bubble">
        <div class="file-icon"><i class='bx bx-file'></i></div>
        <div class="file-info"><strong>${esc(fname)}</strong><span>${fsize}</span></div>
        <a href="${url}" class="dl-btn" download title="Download"><i class='bx bx-download'></i></a>
      </div>
      <div class="meta"><i class='bx bx-file'></i> File &bull; ${time}</div>
    </div>`;
  }

  return "";
}

function scrollToBottom() {
  const el = document.getElementById("chat-messages");
  setTimeout(() => el.scrollTop = el.scrollHeight, 50);
}

// ===================== SEND TEXT =====================
async function sendTextMessage() {
  const input = document.getElementById("msg-input");
  const text = input.value.trim();
  if (!text || !selectedUser) return;
  input.value = "";

  // Optimistic append
  appendMyMessage({ type: "text", content: text, created_at: new Date().toISOString() });

  try {
    const r = await fetch("/api/messages/send", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ sender: ME, receiver: selectedUser, content: text })
    });
    const d = await r.json();
    if (d.id) lastMsgId = Math.max(lastMsgId, d.id);
  } catch (e) {
    showToast("Failed to send message", "error");
  }
}

function appendMyMessage(m) {
  const el = document.getElementById("chat-messages");
  const empty = el.querySelector(".empty-msg");
  if (empty) empty.remove();
  m.sender = ME;
  el.insertAdjacentHTML("beforeend", renderMessage(m));
  scrollToBottom();
}

// ===================== SEND FILE =====================
async function handleFileAttach(event) {
  const file = event.target.files[0];
  if (!file || !selectedUser) return;
  event.target.value = "";

  showToast(`Sending "${file.name}"...`, "info");

  const fd = new FormData();
  fd.append("file", file);
  fd.append("sender", ME);
  fd.append("receiver", selectedUser);

  try {
    const r = await fetch("/api/messages/file", { method: "POST", body: fd });
    const d = await r.json();
    if (d.ok) {
      showToast(`File "${file.name}" sent!`, "success");
      // Reload conversation to show the file message
      setTimeout(loadConversation, 300);
    } else {
      showToast("Failed to send file", "error");
    }
  } catch (e) {
    showToast("Upload error: " + e.message, "error");
  }
}

// ===================== VOICE RECORDING =====================
async function startRecording() {
  if (isRecording || !selectedUser) return;
  try {
    const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    mediaRecorder = new MediaRecorder(stream, { mimeType: "audio/webm;codecs=opus" });
    audioChunks = [];
    mediaRecorder.ondataavailable = e => { if (e.data.size > 0) audioChunks.push(e.data); };
    mediaRecorder.onstop = () => {
      stream.getTracks().forEach(t => t.stop());
      if (audioChunks.length > 0) sendVoiceMessage();
    };
    mediaRecorder.start();
    isRecording = true;
    document.getElementById("btn-voice-rec").classList.add("recording");
  } catch (e) {
    showToast("Microphone access denied", "error");
  }
}

function stopRecording() {
  if (!isRecording || !mediaRecorder) return;
  isRecording = false;
  document.getElementById("btn-voice-rec").classList.remove("recording");
  mediaRecorder.stop();
}

async function sendVoiceMessage() {
  if (!audioChunks.length || !selectedUser) return;
  const blob = new Blob(audioChunks, { type: "audio/webm" });
  audioChunks = [];

  showToast("Sending voice message...", "info");

  const fd = new FormData();
  fd.append("file", blob, `voice_${Date.now()}.webm`);
  fd.append("sender", ME);
  fd.append("receiver", selectedUser);

  try {
    const r = await fetch("/api/messages/voice", { method: "POST", body: fd });
    const d = await r.json();
    if (d.ok) {
      showToast("Voice message sent!", "success");
      setTimeout(loadConversation, 300);
    } else {
      showToast("Failed to send voice", "error");
    }
  } catch (e) {
    showToast("Upload error", "error");
  }
}

// ===================== VOICE CALL (WebSocket) =====================
let voiceWs = null;
let audioCtx = null;
let localStream = null;
let scriptNode = null;
let callPartner = null;
let callActive = false;
let callStartTime = 0;
let callTimerInterval = null;
let incomingCaller = null;

function connectVoiceWs() {
  if (voiceWs && voiceWs.readyState <= 1) return;
  const proto = location.protocol === "https:" ? "wss" : "ws";
  voiceWs = new WebSocket(`${proto}://${location.host}/ws/voice?username=${enc(ME)}`);
  voiceWs.binaryType = "arraybuffer";

  voiceWs.onmessage = (evt) => {
    // Binary = audio data from partner
    if (evt.data instanceof ArrayBuffer) {
      playAudioChunk(evt.data);
      return;
    }
    // Text = JSON signaling
    try {
      const msg = JSON.parse(evt.data);
      handleVoiceSignal(msg);
    } catch(e) {}
  };

  voiceWs.onclose = () => { setTimeout(connectVoiceWs, 3000); };
  voiceWs.onerror = () => { voiceWs.close(); };
}
connectVoiceWs();

function handleVoiceSignal(msg) {
  if (msg.type === "incoming_call") {
    incomingCaller = msg.from;
    showIncomingCall(msg.from);
  } else if (msg.type === "call_started") {
    callPartner = msg.with;
    callActive = true;
    startAudioCapture();
    showActiveCall(msg.with);
  } else if (msg.type === "call_rejected") {
    showToast(`${msg.by} declined the call`, "info");
    hideCallOverlay();
  } else if (msg.type === "call_ended") {
    showToast(`Call ended by ${msg.by}`, "info");
    stopCall();
  } else if (msg.type === "call_error") {
    showToast(msg.msg, "error");
    hideCallOverlay();
  }
}

function startVoiceCall() {
  if (!selectedUser) return;
  if (!voiceWs || voiceWs.readyState !== 1) {
    showToast("Voice connection not ready, retrying...", "error");
    connectVoiceWs();
    return;
  }
  voiceWs.send(JSON.stringify({ type: "call_request", target: selectedUser }));
  showOutgoingCall(selectedUser);
}

function acceptCall() {
  if (!incomingCaller || !voiceWs) return;
  voiceWs.send(JSON.stringify({ type: "call_accept", target: incomingCaller }));
  incomingCaller = null;
}

function rejectCall() {
  if (!incomingCaller || !voiceWs) return;
  voiceWs.send(JSON.stringify({ type: "call_reject", target: incomingCaller }));
  incomingCaller = null;
  hideCallOverlay();
}

function endCall() {
  if (incomingCaller && !callActive) { rejectCall(); return; }
  if (voiceWs && voiceWs.readyState === 1) {
    voiceWs.send(JSON.stringify({ type: "call_end" }));
  }
  stopCall();
}

// ---- Audio Capture ----
async function startAudioCapture() {
  try {
    audioCtx = new AudioContext({ sampleRate: 16000 });
    localStream = await navigator.mediaDevices.getUserMedia({ audio: true });
    const source = audioCtx.createMediaStreamSource(localStream);
    scriptNode = audioCtx.createScriptProcessor(4096, 1, 1);

    scriptNode.onaudioprocess = (e) => {
      if (!callActive || !voiceWs || voiceWs.readyState !== 1) return;
      const float32 = e.inputBuffer.getChannelData(0);
      const int16 = new Int16Array(float32.length);
      for (let i = 0; i < float32.length; i++) {
        int16[i] = Math.max(-32768, Math.min(32767, float32[i] * 32768));
      }
      voiceWs.send(int16.buffer);
    };

    source.connect(scriptNode);
    scriptNode.connect(audioCtx.destination);
  } catch (e) {
    showToast("Microphone access denied", "error");
    endCall();
  }
}

// ---- Audio Playback ----
let playCtx = null;
let nextPlayTime = 0;

function playAudioChunk(buffer) {
  if (!playCtx) {
    playCtx = new AudioContext({ sampleRate: 16000 });
    nextPlayTime = playCtx.currentTime;
  }

  const int16 = new Int16Array(buffer);
  const float32 = new Float32Array(int16.length);
  for (let i = 0; i < int16.length; i++) {
    float32[i] = int16[i] / 32768;
  }

  const audioBuffer = playCtx.createBuffer(1, float32.length, 16000);
  audioBuffer.getChannelData(0).set(float32);

  const src = playCtx.createBufferSource();
  src.buffer = audioBuffer;
  src.connect(playCtx.destination);

  const now = playCtx.currentTime;
  if (nextPlayTime < now) nextPlayTime = now;
  src.start(nextPlayTime);
  nextPlayTime += audioBuffer.duration;
}

// ---- Stop / Cleanup ----
function stopCall() {
  callActive = false;
  callPartner = null;
  incomingCaller = null;

  if (scriptNode) { scriptNode.disconnect(); scriptNode = null; }
  if (localStream) { localStream.getTracks().forEach(t => t.stop()); localStream = null; }
  if (audioCtx) { audioCtx.close().catch(()=>{}); audioCtx = null; }
  if (playCtx) { playCtx.close().catch(()=>{}); playCtx = null; }
  nextPlayTime = 0;

  clearInterval(callTimerInterval);
  hideCallOverlay();
}

// ---- Call UI ----
function showOutgoingCall(target) {
  const ov = document.getElementById("call-overlay");
  ov.style.display = "flex";
  ov.classList.remove("ringing");
  document.getElementById("call-avatar").textContent = target.charAt(0).toUpperCase();
  document.getElementById("call-title").textContent = target;
  document.getElementById("call-subtitle").textContent = "Calling...";
  document.getElementById("call-timer").style.display = "none";
  document.getElementById("call-accept-btn").style.display = "none";
}

function showIncomingCall(from) {
  const ov = document.getElementById("call-overlay");
  ov.style.display = "flex";
  ov.classList.add("ringing");
  document.getElementById("call-avatar").textContent = from.charAt(0).toUpperCase();
  document.getElementById("call-title").textContent = from;
  document.getElementById("call-subtitle").textContent = "Incoming voice call...";
  document.getElementById("call-timer").style.display = "none";
  document.getElementById("call-accept-btn").style.display = "flex";
}

function showActiveCall(partner) {
  const ov = document.getElementById("call-overlay");
  ov.style.display = "flex";
  ov.classList.remove("ringing");
  document.getElementById("call-avatar").textContent = partner.charAt(0).toUpperCase();
  document.getElementById("call-title").textContent = partner;
  document.getElementById("call-subtitle").textContent = "Connected";
  document.getElementById("call-accept-btn").style.display = "none";

  // Start timer
  callStartTime = Date.now();
  const timerEl = document.getElementById("call-timer");
  timerEl.style.display = "block";
  clearInterval(callTimerInterval);
  callTimerInterval = setInterval(() => {
    const sec = Math.floor((Date.now() - callStartTime) / 1000);
    const m = String(Math.floor(sec / 60)).padStart(2, "0");
    const s = String(sec % 60).padStart(2, "0");
    timerEl.textContent = `${m}:${s}`;
  }, 1000);
}

function hideCallOverlay() {
  document.getElementById("call-overlay").style.display = "none";
  clearInterval(callTimerInterval);
}

// ===================== LOGOUT =====================
async function doLogout() {
  if (!confirm("Logout?")) return;
  try { await fetch("/api/logout", { method: "POST", headers: {"Content-Type":"application/json"}, body: JSON.stringify({username: ME}) }); } catch(e){}
  localStorage.removeItem("fsx_token");
  localStorage.removeItem("fsx_username");
  window.location.href = "/login";
}

// ===================== TOAST =====================
function showToast(msg, type="info") {
  document.querySelectorAll(".toast").forEach(t => t.remove());
  const t = document.createElement("div");
  t.className = `toast toast-${type}`;
  const icon = type === "success" ? "bx-check-circle" : type === "error" ? "bx-error-circle" : "bx-info-circle";
  t.innerHTML = `<i class='bx ${icon}'></i> ${esc(msg)}`;
  document.body.appendChild(t);
  setTimeout(() => t.classList.add("show"), 10);
  setTimeout(() => { t.classList.remove("show"); setTimeout(() => t.remove(), 300); }, 3500);
}

// ===================== UTIL =====================
function esc(s) { const d = document.createElement("div"); d.textContent = s; return d.innerHTML; }
function escHtml(s) { return esc(s).replace(/\n/g, "<br>"); }
function enc(s) { return encodeURIComponent(s); }
function formatSize(b) {
  if (!b) return "0 B";
  if (b < 1024) return b + " B";
  if (b < 1048576) return (b/1024).toFixed(1) + " KB";
  return (b/1048576).toFixed(1) + " MB";
}
function timeShort(ds) {
  try {
    const d = new Date(ds + (ds.includes("Z") ? "" : "Z"));
    return d.toLocaleTimeString([], {hour:"2-digit", minute:"2-digit"});
  } catch(e) { return ""; }
}

// ===================== INIT =====================
fetchUsers();
setInterval(fetchUsers, 3000);
setInterval(pollNewMessages, 2000);
