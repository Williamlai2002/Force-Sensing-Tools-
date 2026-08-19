#ifndef WIFI_PORTAL_PAGE_H
#define WIFI_PORTAL_PAGE_H

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
    #status { margin-top: 12px; font-size: 0.9em; line-height: 1.4; }
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
      var submitBtn = document.querySelector('button[type="submit"]');
      
      // Disable form while processing
      submitBtn.disabled = true;
      document.getElementById('ssid').disabled = true;
      document.getElementById('pass').disabled = true;
      
      statusEl.textContent = 'Saving credentials and restarting device...';
      statusEl.style.color = '#3498db'; // Blue
      
      var body = 'ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass);
      
      // Fire and forget. The ESP32 will restart no matter what.
      fetch('/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body
      }).catch(function() {
        // Expected: connection drops because ESP32 restarted
      });

      // Wait 4 seconds for the ESP32 to boot and either connect to STA or fall back to AP.
      setTimeout(function() {
        // Try to ping the AP. 
        // If it responds, the ESP32 is back in AP mode (STA connection failed).
        // If it fails (network error), the AP is gone (STA connection succeeded!).
        fetch('http://192.168.4.1/', { method: 'HEAD', cache: 'no-cache' })
          .then(function() {
            // Still here! Means STA connection failed.
            statusEl.textContent = 'Could not connect to WiFi. The setup network has reappeared. Please check your password and try again.';
            statusEl.style.color = '#e74c3c'; // Red
            
            // Re-enable form so user can retry
            submitBtn.disabled = false;
            document.getElementById('ssid').disabled = false;
            document.getElementById('pass').disabled = false;
          })
          .catch(function() {
            // AP is gone! Success!
            statusEl.innerHTML = 'Saved! Device is restarting.<br><br>Please reconnect your phone/computer to your regular WiFi network, then open:<br><strong style="font-size:1.1em;">http://screwdriver.local</strong>';
            statusEl.style.color = '#2ecc71'; // Green
          });
      }, 4000);
    });
  </script>
</body>
</html>
)rawliteral";

const char PORTAL_DETECT_RESPONSE[] PROGMEM = "Microsoft NCSI";

#endif