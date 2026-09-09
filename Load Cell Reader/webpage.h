#pragma once

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Screwdriver Force & Torque Monitor</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
<style>
  body { font-family: sans-serif; background:#111; color:#eee; text-align:center; padding:15px; }
  h1 { font-size: 1.2em; color:#aaa; font-weight:normal; }
  .readings-container { display: flex; justify-content: center; gap: 30px; margin: 10px 0; }
  .reading-box { background: #1a1a1a; padding: 10px 20px; border-radius: 8px; border: 1px solid #333; }
  .val { font-size: 2.5em; }
  .unit { font-size: 0.5em; color:#888; }
  button { font-size: 1em; padding: 10px 20px; margin: 5px; border-radius: 8px; border: none; cursor: pointer; }
  #startBtn { background:#2ecc71; color:#000; }
  #stopBtn  { background:#e74c3c; color:#000; }
  #exportBtn{ background:#3498db; color:#000; }
  .calBtn   { background:#e67e22; color:#000; }
  button:disabled { opacity: 0.4; cursor: default; }
  #status { color:#888; margin-top: 5px; }
  .panels-row { display: flex; justify-content: center; gap: 20px; flex-wrap: wrap; margin: 15px 0; }
  .panel { background: #222; padding: 15px; border-radius: 10px; width: 320px; text-align: left; }
  .panel h3 { margin-top: 0; font-size: 1em; color: #f39c12; }
  input[type="number"] { padding: 6px; font-size: 1em; width: 90px; border-radius: 5px; border: 1px solid #444; background: #333; color: #fff; text-align: center; }
  .chart-container { max-width: 900px; margin: 15px auto; background: #1a1a1a; padding: 10px; border-radius: 10px; }
  select { padding: 8px; background: #222; color: #fff; border: 1px solid #444; border-radius: 5px; font-size: 1em; }
</style>
</head>
<body>
  <h1>Handheld Screwdriver - Force & Torque Analysis</h1>
  
  <div class="readings-container">
    <div class="reading-box">
      <div>Force</div>
      <div class="val" id="forceVal">--<span class="unit"> N</span></div>
    </div>
    <div class="reading-box">
      <div>Torque</div>
      <div class="val" id="torqueVal">--<span class="unit"> N·m</span></div>
    </div>
  </div>

  <div>
    <button id="startBtn" onclick="startRecording()">Start Recording</button>
    <button id="stopBtn" onclick="stopRecording()" disabled>Stop Recording</button>
    <button id="exportBtn" onclick="exportCSV()" disabled>Export CSV</button>
  </div>
  <div id="status">Not recording</div>

  <div class="panels-row">
    <!-- Force Calibration Panel -->
    <div class="panel">
      <h3>Force Calibration (Load Cell)</h3>
      <div>
        <label>Known Weight (N): </label>
        <input type="number" id="forceWeight" value="9.81" step="any">
      </div>
      <div style="margin-top: 10px;">
        <button class="calBtn" onclick="tareForce()">1. Tare</button>
        <button class="calBtn" onclick="calForce()">2. Calibrate</button>
      </div>
      <div id="forceCalStatus" style="color:#aaa; font-size: 0.85em; margin-top: 6px;">Ready</div>
    </div>

    <!-- Torque Calibration Panel -->
    <div class="panel">
      <h3>Torque Calibration (Strain Gauge)</h3>
      <div>
        <label>Known Torque (N·m): </label>
        <input type="number" id="torqueValInput" value="1.0" step="any">
      </div>
      <div style="margin-top: 10px;">
        <button class="calBtn" onclick="tareTorque()">1. Tare</button>
        <button class="calBtn" onclick="calTorque()">2. Calibrate</button>
      </div>
      <div id="torqueCalStatus" style="color:#aaa; font-size: 0.85em; margin-top: 6px;">Ready</div>
    </div>
  </div>

  <div style="margin-top: 15px;">
    <label for="viewSelector"><b>Graph View Mode: </b></label>
    <select id="viewSelector" onchange="updateViewMode()">
      <option value="combined">Both on One Chart (Dual Axis)</option>
      <option value="force_only">Force Only (N)</option>
      <option value="torque_only">Torque Only (N·m)</option>
      <option value="split">Separate Charts</option>
    </select>
  </div>

  <div class="chart-container" id="combinedContainer">
    <canvas id="combinedChart" height="260"></canvas>
  </div>

  <div class="chart-container" id="splitContainer" style="display:none;">
    <canvas id="forceChart" height="150"></canvas>
    <canvas id="torqueChart" height="150" style="margin-top:15px;"></canvas>
  </div>

<script>
var ws = new WebSocket('ws://' + location.host + '/ws');
var recordedData = [];
var lastDataTime = 0;
var watchdogTimer = null;
var MAX_LIVE_POINTS = 200;

// Initialize Charts using Chart.js v4
var combinedCtx = document.getElementById('combinedChart').getContext('2d');
var combinedChart = new Chart(combinedCtx, {
  type: 'line',
  data: {
    labels: [],
    datasets: [
      { label: 'Force (N)', data: [], borderColor: '#2ecc71', yAxisID: 'y_force', borderWidth: 2, pointRadius: 0, tension: 0.2 },
      { label: 'Torque (N·m)', data: [], borderColor: '#3498db', yAxisID: 'y_torque', borderWidth: 2, pointRadius: 0, tension: 0.2 }
    ]
  },
  options: {
    animation: false,
    scales: {
      x: { ticks: { color: '#888' }, grid: { color: '#333' } },
      y_force: { type: 'linear', position: 'left', ticks: { color: '#2ecc71' }, grid: { color: '#333' } },
      y_torque: { type: 'linear', position: 'right', ticks: { color: '#3498db' }, grid: { display: false } }
    },
    plugins: { legend: { labels: { color: '#eee' } } }
  }
});

var forceCtx = document.getElementById('forceChart').getContext('2d');
var forceChart = new Chart(forceCtx, {
  type: 'line',
  data: { labels: [], datasets: [{ label: 'Force (N)', data: [], borderColor: '#2ecc71', borderWidth: 2, pointRadius: 0, tension: 0.2 }] },
  options: { animation: false, scales: { x: { ticks: { color: '#888' }, grid: { color: '#333' } }, y: { ticks: { color: '#888' }, grid: { color: '#333' } } }, plugins: { legend: { labels: { color: '#eee' } } } }
});

var torqueCtx = document.getElementById('torqueChart').getContext('2d');
var torqueChart = new Chart(torqueCtx, {
  type: 'line',
  data: { labels: [], datasets: [{ label: 'Torque (N·m)', data: [], borderColor: '#3498db', borderWidth: 2, pointRadius: 0, tension: 0.2 }] },
  options: { animation: false, scales: { x: { ticks: { color: '#888' }, grid: { color: '#333' } }, y: { ticks: { color: '#888' }, grid: { color: '#333' } } }, plugins: { legend: { labels: { color: '#eee' } } } }
});

function updateViewMode() {
  var mode = document.getElementById('viewSelector').value;
  var combinedContainer = document.getElementById('combinedContainer');
  var splitContainer = document.getElementById('splitContainer');

  if (mode === 'split') {
    combinedContainer.style.display = 'none';
    splitContainer.style.display = 'block';
  } else {
    combinedContainer.style.display = 'block';
    splitContainer.style.display = 'none';
    
    combinedChart.options.scales.y_force.display = (mode === 'combined' || mode === 'force_only');
    combinedChart.options.scales.y_torque.display = (mode === 'combined' || mode === 'torque_only');
    combinedChart.data.datasets[0].hidden = (mode === 'torque_only');
    combinedChart.data.datasets[1].hidden = (mode === 'force_only');
    combinedChart.update('none');
  }
}

ws.onmessage = function(event) {
  var msg = JSON.parse(event.data);

  if (msg.type === 'live') {
    document.getElementById('forceVal').innerHTML = msg.force.toFixed(2) + '<span class="unit"> N</span>';
    document.getElementById('torqueVal').innerHTML = msg.torque.toFixed(2) + '<span class="unit"> N·m</span>';

    var label = (msg.t / 1000).toFixed(1);
    
    // Update Combined
    combinedChart.data.labels.push(label);
    combinedChart.data.datasets[0].data.push(msg.force);
    combinedChart.data.datasets[1].data.push(msg.torque);
    if (combinedChart.data.labels.length > MAX_LIVE_POINTS) {
      combinedChart.data.labels.shift();
      combinedChart.data.datasets[0].data.shift();
      combinedChart.data.datasets[1].data.shift();
    }
    combinedChart.update('none');

    // Update Split
    forceChart.data.labels.push(label);
    forceChart.data.datasets[0].data.push(msg.force);
    if (forceChart.data.labels.length > MAX_LIVE_POINTS) { forceChart.data.labels.shift(); forceChart.data.datasets[0].data.shift(); }
    forceChart.update('none');

    torqueChart.data.labels.push(label);
    torqueChart.data.datasets[0].data.push(msg.torque);
    if (torqueChart.data.labels.length > MAX_LIVE_POINTS) { torqueChart.data.labels.shift(); torqueChart.data.datasets[0].data.shift(); }
    torqueChart.update('none');

  } else if (msg.type === 'data') {
    var pairs = msg.rows.split(';');
    for (var i = 0; i < pairs.length; i++) {
      var parts = pairs[i].split(',');
      recordedData.push({ t: parseFloat(parts[0]) / 1000, force: parseFloat(parts[1]), torque: parseFloat(parts[2]) });
    }
    lastDataTime = Date.now();
    document.getElementById('status').innerText = 'Receiving recorded data... ' + recordedData.length + ' points';

  } else if (msg.type === 'done') {
    if (watchdogTimer) { clearInterval(watchdogTimer); watchdogTimer = null; }
    document.getElementById('status').innerText = 'Stopped - ' + msg.count + ' points recorded';
    document.getElementById('exportBtn').disabled = recordedData.length === 0;
    document.getElementById('startBtn').disabled = false;
  }
};

ws.onclose = function() {
  document.getElementById('status').innerText = 'Disconnected - reconnecting...';
  setTimeout(function() { location.reload(); }, 2000);
};

startRecording = function() {
  recordedData = [];
  document.getElementById('startBtn').disabled = true;
  document.getElementById('stopBtn').disabled = false;
  document.getElementById('exportBtn').disabled = true;
  document.getElementById('status').innerText = 'Recording Force & Torque...';
  ws.send('START');
};

stopRecording = function() {
  document.getElementById('stopBtn').disabled = true;
  document.getElementById('status').innerText = 'Stopping - retrieving data...';
  ws.send('STOP');

  watchdogTimer = setInterval(function() {
    if (Date.now() - lastDataTime > 2000) {
      ws.send('RESUME:' + recordedData.length);
      lastDataTime = Date.now();
    }
  }, 1000);
};

tareForce = function() {
  document.getElementById('forceCalStatus').innerText = 'Taring Force...';
  fetch('/api?mode=tare_force').then(res => res.json()).then(d => {
    document.getElementById('forceCalStatus').innerText = 'Force Tare Complete!';
  });
};

tareTorque = function() {
  document.getElementById('torqueCalStatus').innerText = 'Taring Torque...';
  fetch('/api?mode=tare_torque').then(res => res.json()).then(d => {
    document.getElementById('torqueCalStatus').innerText = 'Torque Tare Complete!';
  });
};

calForce = function() {
  var w = document.getElementById('forceWeight').value;
  fetch('/api?mode=cal_force&weight=' + w).then(res => res.json()).then(d => {
    document.getElementById('forceCalStatus').innerText = 'Calibrated! Factor: ' + d.factor.toFixed(2);
  });
};

calTorque = function() {
  var t = document.getElementById('torqueValInput').value;
  fetch('/api?mode=cal_torque&torque=' + t).then(res => res.json()).then(d => {
    document.getElementById('torqueCalStatus').innerText = 'Calibrated! Factor: ' + d.factor.toFixed(2);
  });
};

function longestSegment(data) {
  var segments = [];
  var start = 0;
  for (var i = 1; i < data.length; i++) {
    if (data[i].t < data[i - 1].t) {
      segments.push(data.slice(start, i));
      start = i;
    }
  }
  segments.push(data.slice(start));
  var best = segments[0];
  for (var j = 1; j < segments.length; j++) {
    if (segments[j].length > best.length) best = segments[j];
  }
  return best;
}

exportCSV = function() {
  var cleanData = longestSegment(recordedData);
  var csv = "time_seconds,force_newtons,torque_nm\n";
  cleanData.forEach(function(row) {
    csv += row.t.toFixed(3) + ',' + row.force.toFixed(4) + ',' + row.torque.toFixed(4) + '\n';
  });
  var blob = new Blob([csv], { type: 'text/csv' });
  var url = URL.createObjectURL(blob);
  var a = document.createElement('a');
  a.href = url;
  a.download = 'screwdriver_session.csv';
  a.click();
  URL.revokeObjectURL(url);
};
</script>
</body>
</html>
)rawliteral";