#ifndef WIFI_PORTAL_PAGE_H
#define WIFI_PORTAL_PAGE_H

// Served at "/" when the ESP32 is in AP (setup) mode.
// Kept as a separate header, same reason as webpage.h: the Arduino IDE's
// auto-prototype scanner chokes on JS inside raw string literals in the main .ino.

const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Sensor Screwdriver Setup</title>
  <style>
    body { font-family: sans-serif; max-width: 400px; margin: 40px auto; padding: 0 20px; }
    h2 { font-size: 1.2em; }
    label { display: block; margin-top: 16px; font-size: 0.9em; color: #333; }
    input { width: 100%; padding: 8px; margin-top: 4px; box-sizing: border-box; }
    button { margin-top: 20px; width: 100%; padding: 10px; font-size: 1em; }
    #status { margin-top: 12px; font-size: 0.9em; }
  </style>
</head>
<body>
  <h2>Connect the sensor to your WiFi</h2>
  <p>Enter your WiFi network name and password. The device will restart and connect automatically.</p>
  <form id="form">
    <label>Network Name (SSID)</label>
    <input type="text" id="ssid" name="ssid" required>
    <label>Password</label>
    <input type="password" id="pass" name="pass">
    <button type="submit">Save and Connect</button>
  </form>
  <div id="status"></div>
  <script>
    document.getElementById('form').addEventListener('submit', function(e) {
      e.preventDefault();
      var ssid = document.getElementById('ssid').value;
      var pass = document.getElementById('pass').value;
      var statusEl = document.getElementById('status');
      statusEl.textContent = 'Saving...';
      var body = 'ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass);
      fetch('/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body
      })
      .then(function(r) { return r.text(); })
      .then(function(t) {
        statusEl.textContent = 'Saved. Device is restarting and connecting now.';
      })
      .catch(function(err) {
        statusEl.textContent = 'Error saving settings, please try again.';
      });
    });
  </script>
</body>
</html>
)rawliteral";

// Minimal response some OSes want when probing for a captive portal
// (avoids the phone thinking there's no internet and giving up on the redirect)
const char PORTAL_DETECT_RESPONSE[] PROGMEM = "Microsoft NCSI";

#endif