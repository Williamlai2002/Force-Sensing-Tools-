#pragma once

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Live Potentiometer Reading</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
<style>
  body { font-family: sans-serif; background:#111; color:#eee; text-align:center; padding:20px; }
  h1 { font-size: 1.2em; color:#aaa; font-weight:normal; }
  #reading { font-size: 4em; margin: 10px 0; }
  #unit { font-size: 0.4em; color:#888; }
  button { font-size: 1em; padding: 10px 20px; margin: 5px; border-radius: 8px; border: none; cursor: pointer; }
  #startBtn { background:#2ecc71; color:#000; }
  #stopBtn  { background:#e74c3c; color:#000; }
  #exportBtn{ background:#3498db; color:#000; }
  button:disabled { opacity: 0.4; cursor: default; }
  #status { color:#888; margin-top: 5px; }
  canvas { max-width: 900px; margin: 20px auto; }
</style>
</head>
<body>
  <h1>ESP32 - Live A0 Reading</h1>
  <div id="reading">--<span id="unit"> V</span></div>

  <button id="startBtn" onclick="startRecording()">Start Recording</button>
  <button id="stopBtn" onclick="stopRecording()" disabled>Stop Recording</button>
  <button id="exportBtn" onclick="exportCSV()" disabled>Export CSV</button>
  <div id="status">Not recording</div>

  <canvas id="chart" height="300"></canvas>

<script>
var ws = new WebSocket('ws://' + location.host + '/ws');
var recordedData = [];
var expectedCount = 0;
var lastDataTime = 0;
var watchdogTimer = null;

var MAX_LIVE_POINTS = 200;

var ctx = document.getElementById('chart').getContext('2d');
var chart = new Chart(ctx, {
  type: 'line',
  data: {
    labels: [],
    datasets: [{
      label: 'Voltage (V)',
      data: [],
      borderColor: '#2ecc71',
      backgroundColor: 'rgba(46,204,113,0.1)',
      borderWidth: 2,
      pointRadius: 0,
      tension: 0.2
    }]
  },
  options: {
    animation: false,
    scales: {
      x: { ticks: { color: '#888' }, grid: { color: '#333' } },
      y: { min: 0, max: 3.3, ticks: { color: '#888' }, grid: { color: '#333' } }
    },
    plugins: { legend: { display: false } }
  }
});

ws.onmessage = function(event) {
  var msg = JSON.parse(event.data);

  if (msg.type === 'live') {
    document.getElementById('reading').innerHTML = msg.v.toFixed(3) + '<span id="unit"> V</span>';

    var label = (msg.t / 1000).toFixed(1);
    chart.data.labels.push(label);
    chart.data.datasets[0].data.push(msg.v);
    if (chart.data.labels.length > MAX_LIVE_POINTS) {
      chart.data.labels.shift();
      chart.data.datasets[0].data.shift();
    }
    chart.update('none');

} else if (msg.type === 'data') {
    var pairs = msg.rows.split(';');
    for (var i = 0; i < pairs.length; i++) {
      var parts = pairs[i].split(',');
      recordedData.push({ t: parseFloat(parts[0]) / 1000, v: parseFloat(parts[1]) });
    }
    lastDataTime = Date.now();
    document.getElementById('status').innerText =
      'Receiving recorded data... ' + recordedData.length + ' points';


  } else if (msg.type === 'done') {
    if (watchdogTimer) { clearInterval(watchdogTimer); watchdogTimer = null; }
    document.getElementById('status').innerText =
      'Stopped - ' + msg.count + ' points recorded (full resolution)';
    document.getElementById('exportBtn').disabled = recordedData.length === 0;
    document.getElementById('startBtn').disabled = false;

  } else if (msg.type === 'log') {
    console.log('[ESP32]', msg.msg);
  }
};

ws.onclose = function() {
  document.getElementById('status').innerText = 'Disconnected - reconnecting...';
  document.getElementById('startBtn').disabled = true;
  document.getElementById('stopBtn').disabled = true;
  document.getElementById('exportBtn').disabled = true;
  setTimeout(function() { location.reload(); }, 2000);
};

startRecording = function() {
  recordedData = [];
  document.getElementById('startBtn').disabled = true;
  document.getElementById('stopBtn').disabled = false;
  document.getElementById('exportBtn').disabled = true;
  document.getElementById('status').innerText = 'Recording (full resolution, on-device)...';
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

// If a recording session got interrupted and resumed (soft-lock, dropped
// WebSocket messages, etc.), recordedData can end up with more than one
// segment stitched together, each restarting near t=0. This keeps only the
// longest one - i.e. the recording that actually ran to completion - the
// same logic as postprocess_csv.py, just applied automatically before export.
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

  if (segments.length > 1) {
    console.log('[export] found ' + segments.length + ' segments, sizes: ' +
      segments.map(function(s) { return s.length; }).join(', ') +
      ' - keeping the longest (' + best.length + ' rows)');
  }
  return best;
}

exportCSV = function() {
  var cleanData = longestSegment(recordedData);
  var csv = "time_seconds,voltage\n";
  cleanData.forEach(function(row) {
    csv += row.t.toFixed(3) + ',' + row.v.toFixed(4) + '\n';
  });
  var blob = new Blob([csv], { type: 'text/csv' });
  var url = URL.createObjectURL(blob);
  var a = document.createElement('a');
  a.href = url;
  a.download = 'reading_session.csv';
  a.click();
  URL.revokeObjectURL(url);
};
</script>
</body>
</html>
)rawliteral";