#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HX711.h>
#include "webpage.h"
#include "wifi_provision.h"

// WiFi credentials are no longer hardcoded here — they're entered once via
// the setup portal (see WiFiProvision::startAPMode) and stored in flash.

// HX711 load cell amp wiring:
//   DOUT (data) -> GPIO0   (you already have this connected)
//   SCK  (clk)  -> GPIO1   (free of any boot/flash/LED role on this board)
//   VCC  -> 3V3, GND -> GND
const int LOADCELL_DOUT_PIN = D0;
const int LOADCELL_SCK_PIN  = D1;
HX711 scale;

// Raw HX711 counts per unit of force. 1.0 = report raw counts, uncalibrated.
// See the calibration steps in setup() for how to find the real value for
// your load cell — swap this placeholder once you have it.
float calibrationFactor = 1.0;

const unsigned long LIVE_INTERVAL_MS = 100;

#define MAX_RECORD_SAMPLES 8000
uint32_t recordTimes[MAX_RECORD_SAMPLES];
float    recordForce[MAX_RECORD_SAMPLES];
uint32_t recordCount = 0;
uint32_t recordStartTime = 0;
bool recording = false;

bool dumping = false;
uint32_t dumpIndex = 0;
const int DUMP_CHUNK_SIZE = 40;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
unsigned long lastSample = 0;
unsigned long lastLiveSend = 0;

// Sends a line to Serial AND to every connected browser's console (F12 -> Console),
// via the same WebSocket already used for sensor data. Use this in place of
// Serial.print/println/printf wherever you want visibility without a USB cable.
void logMsg(const String &msg) {
  Serial.println(msg);
  if (ws.count() > 0) {
    // Build {"type":"log","msg":"..."} — escape quotes/backslashes so
    // arbitrary text (like heap numbers or error text) can't break the JSON.
    String escaped;
    escaped.reserve(msg.length() + 8);
    for (size_t i = 0; i < msg.length(); i++) {
      char c = msg[i];
      if (c == '"' || c == '\\') escaped += '\\';
      escaped += c;
    }
    ws.textAll("{\"type\":\"log\",\"msg\":\"" + escaped + "\"}");
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      if (len == 5 && memcmp(data, "START", 5) == 0) {
        recording = true;
        recordCount = 0;
        recordStartTime = millis();
        logMsg("Recording started (on-device buffering)");
      } else if (len == 4 && memcmp(data, "STOP", 4) == 0) {
        recording = false;
        dumping = true;
        dumpIndex = 0;
        logMsg("Recording stopped - " + String(recordCount) + " samples captured, sending to browser");
      } else if (len > 7 && memcmp(data, "RESUME:", 7) == 0) {
        char numBuf[16];
        size_t numLen = len - 7;
        if (numLen >= sizeof(numBuf)) numLen = sizeof(numBuf) - 1;
        memcpy(numBuf, data + 7, numLen);
        numBuf[numLen] = '\0';   // properly null-terminate before parsing
        uint32_t fromIndex = atoi(numBuf);
      
        if (fromIndex >= recordCount) {
          char done[40];
          snprintf(done, sizeof(done), "{\"type\":\"done\",\"count\":%u}", recordCount);
          ws.textAll(done);
        } else {
          dumpIndex = fromIndex;
          dumping = true;
          logMsg("Resuming dump from " + String(fromIndex));
        }
      }
    }
  }
}

// Set to true to always boot into the setup portal (AP mode), ignoring any
// WiFi credentials already saved in flash. Useful while testing the portal
// itself. Set back to false once you're done, or every boot forces re-setup.
const bool FORCE_AP_MODE = true;

void setup() {
  Serial.begin(115200);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  Serial.println("Taring load cell - keep it empty/unloaded...");
  delay(1000); // let the HX711 settle after power-up before taring
  scale.tare();
  scale.set_scale(calibrationFactor);
  Serial.println("Tare complete.");
  Serial.println("To calibrate: place a known weight, watch the raw 'v' values");
  Serial.println("in the live readout, then set calibrationFactor = raw_reading / known_weight.");

  if (FORCE_AP_MODE) {
    WiFiProvision::clearCredentials();
    Serial.println("FORCE_AP_MODE is on - cleared saved WiFi, starting setup portal.");
  }

  Serial.println("Trying saved WiFi credentials...");
  bool connected = !FORCE_AP_MODE && WiFiProvision::tryConnectSTA();

  if (connected) {
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());

    WiFiProvision::startMDNS("screwdriver"); // reachable at http://screwdriver.local
    Serial.println("Open http://screwdriver.local in your browser (or the IP above).");

    // Normal dashboard mode: websocket + your existing sensor page
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", INDEX_HTML);
    });

  } else {
    // Serial only here - nobody's connected via WebSocket yet at this point.
    Serial.println("No saved WiFi (or connection failed). Starting setup portal.");
    Serial.println("Connect to the 'Screwdriver-Setup' WiFi network to configure.");

    // Setup mode: ESP32 becomes its own hotspot and serves the credential form.
    // Registered instead of, not alongside, the dashboard routes above.
    WiFiProvision::startAPMode();
    WiFiProvision::setupPortalRoutes(server);
  }

  server.begin();
}

void loop() {
  WiFiProvision::handleLoop(); // no-op unless in AP setup mode

  if (WiFiProvision::isAPMode()) {
    return; // skip sensor sampling/streaming until the device is actually on your network
  }

  ws.cleanupClients();
  unsigned long now = millis();

  // The HX711 paces itself (typically ~10 samples/sec, sometimes 80 if the
  // module's RATE pin is strapped high) - is_ready() is non-blocking and only
  // true when a fresh conversion is waiting, so we just drain it as it comes
  // rather than polling on a fixed timer like the old ADC code did.
  if (scale.is_ready()) {
    lastSample = now;
    float force = scale.get_units(1); // 1 = read a single conversion, no extra averaging delay

    if (!dumping && now - lastLiveSend >= LIVE_INTERVAL_MS && ws.availableForWriteAll()) {
      lastLiveSend = now;
      char json[64];
      snprintf(json, sizeof(json), "{\"type\":\"live\",\"t\":%lu,\"v\":%.4f}", now, force);
      ws.textAll(json);
    }

    if (recording) {
      if (recordCount < MAX_RECORD_SAMPLES) {
        recordTimes[recordCount] = now - recordStartTime;
        recordForce[recordCount] = force;
        recordCount++;
      } else {
        recording = false;
        dumping = true;
        dumpIndex = 0;
        logMsg("Buffer full - recording stopped automatically");
      }
    }
  }

  if (dumping) {
    if (ws.count() == 0) {
      Serial.println("No client connected - aborting dump"); // no clients to send this to, Serial only
      dumping = false;
    } else if (ws.availableForWriteAll()) {
      uint32_t chunkEnd = dumpIndex + DUMP_CHUNK_SIZE;
      if (chunkEnd > recordCount) chunkEnd = recordCount;

      static char buf[1200];
      int pos = 0;
      pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"type\":\"data\",\"rows\":\"");
      for (uint32_t i = dumpIndex; i < chunkEnd; i++) {
        if (i > dumpIndex) buf[pos++] = ';';
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%lu,%.4f", recordTimes[i], recordForce[i]);
      }
      pos += snprintf(buf + pos, sizeof(buf) - pos, "\"}");

      ws.textAll(buf);
      dumpIndex = chunkEnd;

      // Left on Serial only (not logMsg) - this fires every chunk (~every 40
      // samples), so pushing it over the WebSocket too would compete with the
      // actual data dump traffic and spam the browser console.
      Serial.printf("Dump progress: %u / %u, free heap: %u\n", dumpIndex, recordCount, ESP.getFreeHeap());

      if (dumpIndex >= recordCount) {
        char done[40];
        snprintf(done, sizeof(done), "{\"type\":\"done\",\"count\":%u}", recordCount);
        ws.textAll(done);
        dumping = false;
        logMsg("Dump complete");
      }
    }
  }
}
