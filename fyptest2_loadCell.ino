#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HX711.h>

#include "webpage.h"
#include "wifi_provision.h"


// ============================================================
// HX711
// ============================================================

const int LOADCELL_DOUT_PIN = D0;
const int LOADCELL_SCK_PIN  = D1;

HX711 scale;

// Change this after calibration.
float calibrationFactor = 1.0;


// ============================================================
// RECORDING
// ============================================================

#define MAX_RECORD_SAMPLES 8000

uint32_t recordTimes[MAX_RECORD_SAMPLES];
float    recordForce[MAX_RECORD_SAMPLES];

uint32_t recordCount = 0;
uint32_t recordStartTime = 0;

bool recording = false;


// ============================================================
// DATA DUMP
// ============================================================

bool dumping = false;

uint32_t dumpIndex = 0;

const int DUMP_CHUNK_SIZE = 40;


// ============================================================
// WEB SERVER
// ============================================================

AsyncWebServer server(80);

AsyncWebSocket ws("/ws");

unsigned long lastSample = 0;
unsigned long lastLiveSend = 0;

const unsigned long LIVE_INTERVAL_MS = 100;


// ============================================================
// LOGGING
// ============================================================

void logMsg(const String &msg) {

  Serial.println(msg);

  if (ws.count() > 0) {

    String escaped;

    escaped.reserve(msg.length() + 8);

    for (size_t i = 0; i < msg.length(); i++) {

      char c = msg[i];

      if (c == '"' || c == '\\') {
        escaped += '\\';
      }

      escaped += c;
    }

    ws.textAll(
      "{\"type\":\"log\",\"msg\":\"" +
      escaped +
      "\"}"
    );
  }
}


// ============================================================
// WEBSOCKET EVENTS
// ============================================================

void onWsEvent(
  AsyncWebSocket *server,
  AsyncWebSocketClient *client,
  AwsEventType type,
  void *arg,
  uint8_t *data,
  size_t len
) {

  if (type == WS_EVT_CONNECT) {

    Serial.printf(
      "WebSocket client #%u connected\n",
      client->id()
    );

  }

  else if (type == WS_EVT_DISCONNECT) {

    Serial.printf(
      "WebSocket client #%u disconnected\n",
      client->id()
    );

  }

  else if (type == WS_EVT_DATA) {

    AwsFrameInfo *info =
      (AwsFrameInfo*)arg;


    if (
      info->final &&
      info->index == 0 &&
      info->len == len &&
      info->opcode == WS_TEXT
    ) {


      // ======================================================
      // START
      // ======================================================

      if (
        len == 5 &&
        memcmp(data, "START", 5) == 0
      ) {

        recording = true;

        dumping = false;

        recordCount = 0;

        recordStartTime = millis();

        logMsg(
          "Recording started (on-device buffering)"
        );
      }


      // ======================================================
      // STOP
      // ======================================================

      else if (
        len == 4 &&
        memcmp(data, "STOP", 4) == 0
      ) {

        recording = false;

        dumping = true;

        dumpIndex = 0;

        logMsg(
          "Recording stopped - " +
          String(recordCount) +
          " samples captured, sending to browser"
        );
      }


      // ======================================================
      // RESUME
      // ======================================================

      else if (
        len > 7 &&
        memcmp(data, "RESUME:", 7) == 0
      ) {

        char numBuf[16];

        size_t numLen =
          len - 7;


        if (
          numLen >=
          sizeof(numBuf)
        ) {

          numLen =
            sizeof(numBuf) - 1;
        }


        memcpy(
          numBuf,
          data + 7,
          numLen
        );


        numBuf[numLen] = '\0';


        uint32_t fromIndex =
          atoi(numBuf);


        if (
          fromIndex >=
          recordCount
        ) {

          char done[40];

          snprintf(
            done,
            sizeof(done),
            "{\"type\":\"done\",\"count\":%u}",
            recordCount
          );

          ws.textAll(done);

        }

        else {

          dumpIndex =
            fromIndex;

          dumping = true;

          logMsg(
            "Resuming dump from " +
            String(fromIndex)
          );
        }
      }
    }
  }
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);


  // ==========================================================
  // HX711
  // ==========================================================

  scale.begin(
    LOADCELL_DOUT_PIN,
    LOADCELL_SCK_PIN
  );


  Serial.println(
    "Taring load cell - keep it empty/unloaded..."
  );


  delay(1000);


  scale.tare();


  scale.set_scale(
    calibrationFactor
  );


  Serial.println(
    "Tare complete."
  );


  Serial.println(
    "To calibrate: place a known weight, "
    "watch the raw 'v' values in the live readout, "
    "then set calibrationFactor = raw_reading / known_weight."
  );


  // ==========================================================
  // WIFI
  // ==========================================================

  Serial.println(
    "Trying saved WiFi credentials..."
  );


  bool connected =
    WiFiProvision::tryConnectSTA();


  // ==========================================================
  // STA MODE
  // ==========================================================

  if (connected) {

    Serial.print(
      "Connected! IP address: "
    );

    Serial.println(
      WiFi.localIP()
    );


    WiFiProvision::startMDNS(
      "screwdriver"
    );


    Serial.println(
      "Open http://screwdriver.local "
      "in your browser."
    );


    // --------------------------------------------------------
    // WebSocket
    // --------------------------------------------------------

    ws.onEvent(
      onWsEvent
    );

    server.addHandler(
      &ws
    );


    // --------------------------------------------------------
    // Dashboard
    // --------------------------------------------------------

    server.on(
      "/",
      HTTP_GET,
      [](AsyncWebServerRequest *request) {

        Serial.println(
          "Serving main dashboard."
        );

        request->send_P(
          200,
          "text/html",
          INDEX_HTML
        );
      }
    );


    // --------------------------------------------------------
    // UNIFIED API ENDPOINT (For Python / External Tools)
    // --------------------------------------------------------

    server.on("/api", HTTP_GET, [](AsyncWebServerRequest *request) {
      
      String mode = "";
      if (request->hasParam("mode")) {
        mode = request->getParam("mode")->value();
      }

      if (mode == "live") {
        float currentForce = scale.get_units(1);
        char json[128];
        snprintf(json, sizeof(json), 
          "{\"mode\":\"live\",\"t\":%lu,\"force\":%.4f}",
          millis(),
          currentForce
        );
        request->send(200, "application/json", json);
      }
      else if (mode == "recorded") {
        String json = "{\"mode\":\"recorded\",\"count\":" + String(recordCount) + ",\"data\":[";
        for (uint32_t i = 0; i < recordCount; i++) {
          if (i > 0) json += ",";
          json += "{\"t\":" + String(recordTimes[i]) + ",\"force\":" + String(recordForce[i], 4) + "}";
        }
        json += "]}";
        request->send(200, "application/json", json);
      }
      else if (mode == "status") {
        char json[256];
        snprintf(json, sizeof(json),
          "{\"mode\":\"status\",\"recording\":%s,\"recordCount\":%u,\"freeHeap\":%u,\"uptime\":%lu}",
          recording ? "true" : "false",
          recordCount,
          ESP.getFreeHeap(),
          millis() / 1000
        );
        request->send(200, "application/json", json);
      }
      else if (mode == "start") {
        recording = true;
        dumping = false;
        recordCount = 0;
        recordStartTime = millis();
        request->send(200, "application/json", "{\"mode\":\"start\",\"status\":\"recording\"}");
      }
      else if (mode == "stop") {
        recording = false;
        char json[64];
        snprintf(json, sizeof(json),
          "{\"mode\":\"stop\",\"status\":\"stopped\",\"count\":%u}",
          recordCount
        );
        request->send(200, "application/json", json);
      }
      else if (mode == "clear") {
        recordCount = 0;
        recording = false;
        dumping = false;
        request->send(200, "application/json", "{\"mode\":\"clear\",\"status\":\"cleared\"}");
      }
      else if (mode == "info") {
        char json[256];
        snprintf(json, sizeof(json),
          "{\"mode\":\"info\",\"ip\":\"%s\",\"rssi\":%d,\"calibration\":%.2f,\"maxSamples\":%u}",
          WiFi.localIP().toString().c_str(),
          WiFi.RSSI(),
          calibrationFactor,
          MAX_RECORD_SAMPLES
        );
        request->send(200, "application/json", json);
      }
      else {
        request->send(400, "application/json", 
          "{\"error\":\"Invalid or missing mode parameter\",\"valid_modes\":[\"live\",\"recorded\",\"status\",\"start\",\"stop\",\"clear\",\"info\"]}"
        );
      }
    });

  }


  // ==========================================================
  // AP MODE
  // ==========================================================

  else {

    Serial.println(
      "No saved WiFi "
      "(or connection failed)."
    );


    Serial.println(
      "Starting setup portal."
    );


    Serial.println(
      "Connect to the "
      "'Screwdriver-Setup' WiFi network."
    );


    WiFiProvision::startAPMode();


    WiFiProvision::setupPortalRoutes(
      server
    );
  }


  // ==========================================================
  // START SERVER
  // ==========================================================

  server.begin();


  Serial.println(
    "Web server started."
  );
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  // Handles DNS requests when AP setup mode is active.
  WiFiProvision::handleLoop();


  // ==========================================================
  // IMPORTANT
  //
  // In AP setup mode we don't run the sensor dashboard.
  // Once STA connects, this returns false and the HX711 runs.
  // ==========================================================

  if (
    WiFiProvision::isAPMode()
  ) {

    return;
  }


  ws.cleanupClients();


  unsigned long now =
    millis();


  // ==========================================================
  // HX711 SAMPLE
  // ==========================================================

  if (
    scale.is_ready()
  ) {

    lastSample = now;


    float force =
      scale.get_units(1);


    // ========================================================
    // LIVE DATA
    // ========================================================

    if (
      !dumping &&
      now - lastLiveSend >=
      LIVE_INTERVAL_MS &&
      ws.availableForWriteAll()
    ) {

      lastLiveSend = now;


      char json[64];


      snprintf(
        json,
        sizeof(json),
        "{\"type\":\"live\",\"t\":%lu,\"v\":%.4f}",
        now,
        force
      );


      ws.textAll(
        json
      );
    }


    // ========================================================
    // RECORDING
    // ========================================================

    if (recording) {

      if (
        recordCount <
        MAX_RECORD_SAMPLES
      ) {

        recordTimes[recordCount] =
          now - recordStartTime;


        recordForce[recordCount] =
          force;


        recordCount++;

      }

      else {

        recording = false;

        dumping = true;

        dumpIndex = 0;


        logMsg(
          "Buffer full - recording stopped automatically"
        );
      }
    }
  }


  // ==========================================================
  // SEND RECORDED DATA
  // ==========================================================

  if (dumping) {

    if (
      ws.count() == 0
    ) {

      Serial.println(
        "No client connected - aborting dump"
      );


      dumping = false;
    }


    else if (
      ws.availableForWriteAll()
    ) {

      uint32_t chunkEnd =
        dumpIndex +
        DUMP_CHUNK_SIZE;


      if (
        chunkEnd >
        recordCount
      ) {

        chunkEnd =
          recordCount;
      }


      static char buf[1200];

      int pos = 0;


      pos += snprintf(
        buf + pos,
        sizeof(buf) - pos,
        "{\"type\":\"data\",\"rows\":\""
      );


      for (
        uint32_t i = dumpIndex;
        i < chunkEnd;
        i++
      ) {

        if (
          i > dumpIndex
        ) {

          buf[pos++] = ';';
        }


        pos += snprintf(
          buf + pos,
          sizeof(buf) - pos,
          "%lu,%.4f",
          recordTimes[i],
          recordForce[i]
        );
      }


      pos += snprintf(
        buf + pos,
        sizeof(buf) - pos,
        "\"}"
      );


      ws.textAll(
        buf
      );


      dumpIndex =
        chunkEnd;


      Serial.printf(
        "Dump progress: %u / %u, free heap: %u\n",
        dumpIndex,
        recordCount,
        ESP.getFreeHeap()
      );


      if (
        dumpIndex >=
        recordCount
      ) {

        char done[40];


        snprintf(
          done,
          sizeof(done),
          "{\"type\":\"done\",\"count\":%u}",
          recordCount
        );


        ws.textAll(
          done
        );


        dumping = false;


        logMsg(
          "Dump complete"
        );
      }
    }
  }
}