#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HX711.h>

#include "webpage.h"
#include "wifi_provision.h"


// ============================================================
// SENSORS
// ============================================================

// ---------------- FORCE / HX711 ----------------

const int LOADCELL_DOUT_PIN = 3;
const int LOADCELL_SCK_PIN  = 2;

HX711 scaleForce;

// Built-in starting calibration factor.
// This is used when the ESP32 boots.
float forceCalibrationFactor = 42.0;

// Raw HX711 zero/tare value.
long forceZeroRaw = 0;

// Gravity conversion.
const float GRAVITY = 9.80665;


// ---------------- TORQUE ----------------

const int TORQUE_ANALOG_PIN = 0;

float torqueZeroVoltage = 0.0;

// Original calibration:
// 0.38 Nm = 1100 ADC counts
float torqueCalibrationFactor = 0.38 / 1100.0;


// ============================================================
// RECORDING
// ============================================================

#define MAX_RECORD_SAMPLES 8000

uint32_t recordTimes[MAX_RECORD_SAMPLES];
float    recordForce[MAX_RECORD_SAMPLES];
float    recordTorque[MAX_RECORD_SAMPLES];

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
// FORCE SENSOR FUNCTIONS
// ============================================================

// Read raw HX711 value.
long readForceRaw() {
  return scaleForce.read_average(20);
}


// ------------------------------------------------------------
// TARE FORCE
// ------------------------------------------------------------

void tareForceSensor() {

  Serial.println(
    "Force tare started - remove all load from load cell."
  );

  // HX711 library performs averaging and stores the offset.
  scaleForce.tare(20);

  // Read back the actual raw zero value.
  forceZeroRaw = scaleForce.read_average(5);

  // Keep the current calibration factor.
  scaleForce.set_scale(forceCalibrationFactor);

  Serial.print("Force zero raw = ");
  Serial.println(forceZeroRaw);

  Serial.print("Force calibration factor = ");
  Serial.println(forceCalibrationFactor, 6);

  logMsg(
    "Force tare complete. Raw zero = " +
    String(forceZeroRaw)
  );
}


// ------------------------------------------------------------
// CALIBRATE FORCE
//
// knownMassKg = physical calibration mass in kilograms.
//
// Example:
// 4 kg -> 39.2266 N
//
// calibration factor:
//
//     raw difference
// -----------------------
//       force in N
//
// HX711 get_units() effectively does:
//
//     (raw - zero) / calibration factor
// ------------------------------------------------------------

bool calibrateForce(float knownMassKg, float &newFactor, long &rawReading) {

  if (knownMassKg <= 0.0) {
    return false;
  }

  // Convert kg to Newtons.
  float knownForceN = knownMassKg * GRAVITY;

  Serial.println();
  Serial.println("========== FORCE CALIBRATION ==========");
  Serial.print("Known mass: ");
  Serial.print(knownMassKg, 4);
  Serial.println(" kg");

  Serial.print("Known force: ");
  Serial.print(knownForceN, 4);
  Serial.println(" N");

  // Read raw HX711 value with calibration weight applied.
  rawReading = scaleForce.read_average(20);

  Serial.print("Raw loaded reading: ");
  Serial.println(rawReading);

  Serial.print("Raw zero reading: ");
  Serial.println(forceZeroRaw);

  // Difference between loaded and zero.
  long rawDifference = rawReading - forceZeroRaw;

  Serial.print("Raw difference: ");
  Serial.println(rawDifference);

  if (rawDifference == 0) {
    Serial.println("ERROR: No change detected from load cell.");
    return false;
  }

  // Calculate calibration factor.
  newFactor = (float)rawDifference / knownForceN;

  // Apply the new factor immediately.
  forceCalibrationFactor = newFactor;
  scaleForce.set_scale(forceCalibrationFactor);

  Serial.print("NEW FORCE CALIBRATION FACTOR = ");
  Serial.println(forceCalibrationFactor, 6);

  // Check resulting measurement.
  float measuredForce = scaleForce.get_units(10);

  Serial.print("Measured force after calibration: ");
  Serial.print(measuredForce, 4);
  Serial.println(" N");

  Serial.println("========================================");
  Serial.println();

  return true;
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

    AwsFrameInfo *info = (AwsFrameInfo*)arg;

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

        size_t numLen = len - 7;

        if (numLen >= sizeof(numBuf)) {
          numLen = sizeof(numBuf) - 1;
        }

        memcpy(
          numBuf,
          data + 7,
          numLen
        );

        numBuf[numLen] = '\0';

        uint32_t fromIndex = atoi(numBuf);

        if (fromIndex >= recordCount) {

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

          dumpIndex = fromIndex;
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

  delay(1000);

  // ESP32-C3 ADC resolution.
  analogReadResolution(12);


  // ==========================================================
  // FORCE SENSOR INIT
  // ==========================================================

  scaleForce.begin(
    LOADCELL_DOUT_PIN,
    LOADCELL_SCK_PIN
  );

  Serial.println();
  Serial.println("======================================");
  Serial.println("FORCE SENSOR INITIALISATION");
  Serial.println("======================================");

  Serial.print(
    "Starting calibration factor: "
  );

  Serial.println(
    forceCalibrationFactor,
    6
  );

  Serial.println(
    "Taring force sensor..."
  );

  delay(1000);

  // Tare the HX711.
  scaleForce.tare(20);

  // Read actual raw zero.
  forceZeroRaw = scaleForce.read_average(5);

  // Apply built-in calibration factor.
  scaleForce.set_scale(
    forceCalibrationFactor
  );

  Serial.print(
    "Force raw zero = "
  );

  Serial.println(
    forceZeroRaw
  );

  Serial.print(
    "Force calibration factor = "
  );

  Serial.println(
    forceCalibrationFactor,
    6
  );


  // ==========================================================
  // TORQUE SENSOR INIT
  // ==========================================================

  pinMode(
    TORQUE_ANALOG_PIN,
    INPUT
  );

  Serial.println(
    "Taring torque sensor..."
  );

  long analogSum = 0;

  for (int i = 0; i < 100; i++) {

    analogSum += analogRead(
      TORQUE_ANALOG_PIN
    );

    delay(5);
  }

  torqueZeroVoltage =
    (float)analogSum / 100.0;

  Serial.print(
    "Torque zero ADC = "
  );

  Serial.println(
    torqueZeroVoltage,
    2
  );

  Serial.println(
    "Sensor tare complete."
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
      "Open http://screwdriver.local in your browser."
    );


    ws.onEvent(onWsEvent);

    server.addHandler(&ws);


    // ========================================================
    // MAIN WEBPAGE
    // ========================================================

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


    // ========================================================
    // UNIFIED API
    // ========================================================

    server.on(
      "/api",
      HTTP_GET,
      [](AsyncWebServerRequest *request) {

        String mode = "";

        if (request->hasParam("mode")) {
          mode =
            request->getParam("mode")->value();
        }


        // ====================================================
        // LIVE
        // ====================================================

        if (mode == "live") {

          float currentForce =
            scaleForce.get_units(5);


          long strainTotal = 0;

          for (int i = 0; i < 10; i++) {

            strainTotal +=
              analogRead(
                TORQUE_ANALOG_PIN
              );

            delay(1);
          }


          float currentTorque =
            (
              ((float)strainTotal / 10.0)
              -
              torqueZeroVoltage
            )
            *
            torqueCalibrationFactor;


          char json[160];

          snprintf(
            json,
            sizeof(json),

            "{\"mode\":\"live\",\"t\":%lu,\"force\":%.4f,\"torque\":%.4f}",

            millis(),

            currentForce,

            currentTorque
          );

          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // RECORDED
        // ====================================================

        else if (mode == "recorded") {

          String json =
            "{\"mode\":\"recorded\",\"count\":" +
            String(recordCount) +
            ",\"data\":[";


          for (
            uint32_t i = 0;
            i < recordCount;
            i++
          ) {

            if (i > 0) {
              json += ",";
            }

            json +=
              "{\"t\":" +
              String(recordTimes[i]) +
              ",\"force\":" +
              String(recordForce[i], 4) +
              ",\"torque\":" +
              String(recordTorque[i], 4) +
              "}";
          }


          json += "]}";


          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // STATUS
        // ====================================================

        else if (mode == "status") {

          char json[320];

          snprintf(
            json,
            sizeof(json),

            "{\"mode\":\"status\","
            "\"recording\":%s,"
            "\"recordCount\":%u,"
            "\"freeHeap\":%u,"
            "\"uptime\":%lu,"
            "\"forceFactor\":%.6f,"
            "\"forceZeroRaw\":%ld}",

            recording ? "true" : "false",

            recordCount,

            ESP.getFreeHeap(),

            millis() / 1000,

            forceCalibrationFactor,

            forceZeroRaw
          );


          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // START
        // ====================================================

        else if (mode == "start") {

          recording = true;
          dumping = false;

          recordCount = 0;

          recordStartTime = millis();


          request->send(
            200,
            "application/json",
            "{\"mode\":\"start\",\"status\":\"recording\"}"
          );
        }


        // ====================================================
        // STOP
        // ====================================================

        else if (mode == "stop") {

          recording = false;

          char json[64];

          snprintf(
            json,
            sizeof(json),

            "{\"mode\":\"stop\",\"status\":\"stopped\",\"count\":%u}",

            recordCount
          );


          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // CLEAR
        // ====================================================

        else if (mode == "clear") {

          recordCount = 0;

          recording = false;

          dumping = false;


          request->send(
            200,
            "application/json",
            "{\"mode\":\"clear\",\"status\":\"cleared\"}"
          );
        }


        // ====================================================
        // INFO
        // ====================================================

        else if (mode == "info") {

          char json[320];

          snprintf(
            json,
            sizeof(json),

            "{\"mode\":\"info\","
            "\"ip\":\"%s\","
            "\"rssi\":%d,"
            "\"forceCalibration\":%.6f,"
            "\"forceZeroRaw\":%ld,"
            "\"torqueCalibration\":%.6f,"
            "\"maxSamples\":%u}",

            WiFi.localIP().toString().c_str(),

            WiFi.RSSI(),

            forceCalibrationFactor,

            forceZeroRaw,

            torqueCalibrationFactor,

            MAX_RECORD_SAMPLES
          );


          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // TARE BOTH
        // ====================================================

        else if (mode == "tare") {

          // FORCE

          scaleForce.tare(20);

          forceZeroRaw =
            scaleForce.read_average(5);

          scaleForce.set_scale(
            forceCalibrationFactor
          );


          // TORQUE

          long sum = 0;

          for (int i = 0; i < 100; i++) {

            sum +=
              analogRead(
                TORQUE_ANALOG_PIN
              );

            delay(5);
          }

          torqueZeroVoltage =
            (float)sum / 100.0;


          char json[180];

          snprintf(
            json,
            sizeof(json),

            "{\"mode\":\"tare\","
            "\"status\":\"success\","
            "\"forceZeroRaw\":%ld,"
            "\"torqueZeroRaw\":%.2f}",

            forceZeroRaw,

            torqueZeroVoltage
          );


          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // TARE FORCE ONLY
        // ====================================================

        else if (mode == "tare_force") {

          Serial.println(
            "Web request: FORCE TARE"
          );

          scaleForce.tare(20);

          forceZeroRaw =
            scaleForce.read_average(5);

          scaleForce.set_scale(
            forceCalibrationFactor
          );


          Serial.print(
            "New force zero raw = "
          );

          Serial.println(
            forceZeroRaw
          );


          char json[180];

          snprintf(
            json,
            sizeof(json),

            "{\"mode\":\"tare_force\","
            "\"status\":\"success\","
            "\"zeroRaw\":%ld,"
            "\"factor\":%.6f}",

            forceZeroRaw,

            forceCalibrationFactor
          );


          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // TARE TORQUE ONLY
        // ====================================================

        else if (mode == "tare_torque") {

          long sum = 0;

          for (int i = 0; i < 100; i++) {

            sum +=
              analogRead(
                TORQUE_ANALOG_PIN
              );

            delay(5);
          }


          torqueZeroVoltage =
            (float)sum / 100.0;


          char json[160];

          snprintf(
            json,
            sizeof(json),

            "{\"mode\":\"tare_torque\","
            "\"status\":\"success\","
            "\"zeroRaw\":%.2f}",

            torqueZeroVoltage
          );


          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // CALIBRATE FORCE
        // ====================================================

        else if (mode == "cal_force") {

          float knownMassKg = 0.0;

          if (
            request->hasParam("weight")
          ) {

            knownMassKg =
              request
                ->getParam("weight")
                ->value()
                .toFloat();
          }


          float newFactor = 0.0;

          long rawReading = 0;


          bool success =
            calibrateForce(
              knownMassKg,
              newFactor,
              rawReading
            );


          if (!success) {

            request->send(
              400,
              "application/json",

              "{\"mode\":\"cal_force\","
              "\"status\":\"error\","
              "\"msg\":\"Invalid weight or no load-cell change detected\"}"
            );

            return;
          }


          float knownForceN =
            knownMassKg * GRAVITY;


          char json[240];

          snprintf(
            json,
            sizeof(json),

            "{\"mode\":\"cal_force\","
            "\"status\":\"success\","
            "\"factor\":%.6f,"
            "\"weightKg\":%.4f,"
            "\"forceN\":%.4f,"
            "\"raw\":%ld,"
            "\"zeroRaw\":%ld}",

            newFactor,

            knownMassKg,

            knownForceN,

            rawReading,

            forceZeroRaw
          );


          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // CALIBRATE FORCE - ALIAS
        // ====================================================

        else if (mode == "calibrate_force") {

          float knownMassKg =
            request->hasParam("weight")
              ?
            request
              ->getParam("weight")
              ->value()
              .toFloat()
              :
            0.0;


          float newFactor = 0.0;

          long rawReading = 0;


          bool success =
            calibrateForce(
              knownMassKg,
              newFactor,
              rawReading
            );


          if (!success) {

            request->send(
              400,
              "application/json",

              "{\"status\":\"error\"}"
            );

            return;
          }


          char json[160];

          snprintf(
            json,
            sizeof(json),

            "{\"mode\":\"calibrate_force\","
            "\"status\":\"success\","
            "\"factor\":%.6f}",

            newFactor
          );


          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // CALIBRATE TORQUE
        // ====================================================

        else if (mode == "cal_torque") {

          float knownTorque =
            request->hasParam("torque")
              ?
            request
              ->getParam("torque")
              ->value()
              .toFloat()
              :
            0.38;


          long readingSum = 0;

          for (int i = 0; i < 100; i++) {

            readingSum +=
              analogRead(
                TORQUE_ANALOG_PIN
              );

            delay(5);
          }


          float currentReading =
            (float)readingSum / 100.0;


          float adcChange =
            currentReading -
            torqueZeroVoltage;


          if (
            knownTorque <= 0 ||
            adcChange == 0
          ) {

            request->send(
              400,
              "application/json",

              "{\"mode\":\"cal_torque\","
              "\"status\":\"error\","
              "\"msg\":\"Invalid torque or no ADC change detected\"}"
            );

            return;
          }


          torqueCalibrationFactor =
            knownTorque / adcChange;


          char json[160];

          snprintf(
            json,
            sizeof(json),

            "{\"mode\":\"cal_torque\","
            "\"status\":\"success\","
            "\"factor\":%.6f}",

            torqueCalibrationFactor
          );


          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // CALIBRATE TORQUE - ALIAS
        // ====================================================

        else if (mode == "calibrate_torque") {

          float knownTorque =
            request->hasParam("torque")
              ?
            request
              ->getParam("torque")
              ->value()
              .toFloat()
              :
            0.38;


          long readingSum = 0;

          for (int i = 0; i < 100; i++) {

            readingSum +=
              analogRead(
                TORQUE_ANALOG_PIN
              );

            delay(5);
          }


          float currentReading =
            (float)readingSum / 100.0;


          float adcChange =
            currentReading -
            torqueZeroVoltage;


          if (
            knownTorque > 0 &&
            adcChange != 0
          ) {

            torqueCalibrationFactor =
              knownTorque / adcChange;
          }


          char json[128];

          snprintf(
            json,
            sizeof(json),

            "{\"mode\":\"calibrate_torque\","
            "\"status\":\"success\","
            "\"factor\":%.6f}",

            torqueCalibrationFactor
          );


          request->send(
            200,
            "application/json",
            json
          );
        }


        // ====================================================
        // INVALID
        // ====================================================

        else {

          request->send(
            400,
            "application/json",

            "{\"error\":\"Invalid or missing mode parameter\"}"
          );
        }
      }
    );
  }


  // ==========================================================
  // AP MODE
  // ==========================================================

  else {

    Serial.println(
      "No saved WiFi (or connection failed)."
    );

    Serial.println(
      "Starting setup portal. Connect to 'Screwdriver-Setup'."
    );

    WiFiProvision::startAPMode();

    WiFiProvision::setupPortalRoutes(server);
  }


  server.begin();

  Serial.println(
    "Web server started."
  );
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  WiFiProvision::handleLoop();


  if (WiFiProvision::isAPMode()) {
    return;
  }


  ws.cleanupClients();

  unsigned long now = millis();


  // ==========================================================
  // SENSOR SAMPLE
  // ==========================================================

  if (scaleForce.is_ready()) {

    lastSample = now;


    // FORCE

    float force =
      scaleForce.get_units(1);


    // TORQUE

    long strainTotal = 0;

    for (int i = 0; i < 10; i++) {

      strainTotal +=
        analogRead(
          TORQUE_ANALOG_PIN
        );

      delay(1);
    }


    float torque =
      (
        ((float)strainTotal / 10.0)
        -
        torqueZeroVoltage
      )
      *
      torqueCalibrationFactor;


    // ========================================================
    // LIVE DATA
    // ========================================================

    if (
      !dumping &&
      now - lastLiveSend >= LIVE_INTERVAL_MS &&
      ws.availableForWriteAll()
    ) {

      lastLiveSend = now;


      char json[96];

      snprintf(
        json,
        sizeof(json),

        "{\"type\":\"live\","
        "\"t\":%lu,"
        "\"force\":%.4f,"
        "\"torque\":%.4f}",

        now,

        force,

        torque
      );


      ws.textAll(json);
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

        recordTorque[recordCount] =
          torque;

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

    if (ws.count() == 0) {

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


      static char buf[1400];

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

        if (i > dumpIndex) {
          buf[pos++] = ';';
        }


        pos += snprintf(
          buf + pos,
          sizeof(buf) - pos,

          "%lu,%.4f,%.4f",

          recordTimes[i],

          recordForce[i],

          recordTorque[i]
        );
      }


      pos += snprintf(
        buf + pos,
        sizeof(buf) - pos,

        "\"}"
      );


      ws.textAll(buf);


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


        ws.textAll(done);


        dumping = false;


        logMsg(
          "Dump complete"
        );
      }
    }
  }
}
