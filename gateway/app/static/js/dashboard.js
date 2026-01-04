(function () {
    // WebSocket status
    const statusEl = document.getElementById("status");
    const ws = new WebSocket(`ws://${location.host}/ws`);
  
    ws.onopen = () => { statusEl.textContent = "WS connected"; };
    ws.onmessage = (evt) => {
      const data = JSON.parse(evt.data);
      statusEl.textContent = `WS alive | ts=${data.ts.toFixed(2)} | ${data.status}`;
    };
    ws.onclose = () => { statusEl.textContent = "WS closed"; };
    ws.onerror = () => { statusEl.textContent = "WS error"; };

    // Online users table
    const onlineTbody = document.getElementById("online-tbody");
    const onlineCountEl = document.getElementById("online-count");
    const onlineErrorEl = document.getElementById("online-error");

    function updateOnlineUsers() {
      fetch("/api/online")
        .then(response => {
          if (!response.ok) {
            if (response.status === 503 || response.status === 502) {
              return response.json().then(data => {
                throw new Error(data.detail || "Core server unavailable");
              });
            }
            throw new Error(`HTTP ${response.status}`);
          }
          return response.json();
        })
        .then(data => {
          // Hide error
          onlineErrorEl.style.display = "none";
          
          // Update count
          onlineCountEl.textContent = data.count || 0;
          
          // Update table
          if (data.users && data.users.length > 0) {
            onlineTbody.innerHTML = data.users.map(username => 
              `<tr><td>${escapeHtml(username)}</td></tr>`
            ).join("");
          } else {
            onlineTbody.innerHTML = '<tr><td colspan="1" class="empty-state">No users online</td></tr>';
          }
        })
        .catch(error => {
          // Show error
          onlineErrorEl.textContent = `Error: ${error.message}`;
          onlineErrorEl.style.display = "block";
          onlineCountEl.textContent = "-";
          onlineTbody.innerHTML = '<tr><td colspan="1" class="empty-state">Unable to load</td></tr>';
        });
    }

    function escapeHtml(text) {
      const div = document.createElement("div");
      div.textContent = text;
      return div.innerHTML;
    }

    // Initial load
    updateOnlineUsers();
    
    // Auto-refresh every 2 seconds
    setInterval(updateOnlineUsers, 2000);
  })();
  