(function () {
    const el = document.getElementById("status");
    const ws = new WebSocket(`ws://${location.host}/ws`);
  
    ws.onopen = () => { el.textContent = "WS connected"; };
    ws.onmessage = (evt) => {
      const data = JSON.parse(evt.data);
      el.textContent = `WS alive | ts=${data.ts.toFixed(2)} | ${data.status}`;
    };
    ws.onclose = () => { el.textContent = "WS closed"; };
    ws.onerror = () => { el.textContent = "WS error"; };
  })();
  