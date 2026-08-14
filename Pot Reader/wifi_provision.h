#ifndef WIFI_PROVISION_H
#define WIFI_PROVISION_H

// Requires (already in your project): ESPAsyncWebServer, AsyncTCP
// Also needs: DNSServer, Preferences, WiFi (all bundled with ESP32 core, no extra install)
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include "wifi_portal_page.h"

namespace WiFiProvision {

  // ---- internal state ----
  Preferences prefs;
  DNSServer dnsServer;
  bool apModeActive = false;
  const byte DNS_PORT = 53;
  const char* AP_NAME = "Screwdriver-Setup";   // change per device if you build several units
  const IPAddress AP_IP(192, 168, 4, 1);

  // ---- storage ----
  // "wifi" here is the NVS namespace name, not related to WiFi.h itself.
  // Preferences writes to a dedicated flash partition, so these values
  // survive re-flashing your sketch (unlike variables in the .ino).

  bool hasSavedCredentials() {
    prefs.begin("wifi", true); // read-only
    bool has = prefs.isKey("ssid");
    prefs.end();
    return has;
  }

  void saveCredentials(const String& ssid, const String& pass) {
    prefs.begin("wifi", false); // read-write
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
  }

  void clearCredentials() {
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();
  }

  // ---- station mode ----

  // Tries to connect using saved credentials. Returns true on success.
  // timeoutMs is how long to wait before giving up and falling back to AP mode.
  bool tryConnectSTA(unsigned long timeoutMs = 10000) {
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();

    if (ssid.length() == 0) return false;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
      delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
  }

  // ---- AP + captive portal mode ----

  void startAPMode() {
    apModeActive = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_NAME); // open network, no password, so it's easy to join for setup

    // Redirect every DNS lookup to our own IP, so "captive portal detection"
    // on most phones/laptops auto-opens our page.
    dnsServer.start(DNS_PORT, "*", AP_IP);
  }

  // Call every loop() iteration while in AP mode (harmless/cheap if not).
  void handleLoop() {
    if (apModeActive) {
      dnsServer.processNextRequest();
    }
  }

  // Registers the setup-portal routes on your existing AsyncWebServer instance.
  // Call this once in setup(), after you create your `server` object and
  // before server.begin() — it's fine to also keep your normal dashboard
  // routes registered on the same server object.
  void setupPortalRoutes(AsyncWebServer &server) {

    // Main setup form.
    // Only call setupPortalRoutes() when you're actually in AP mode (see the
    // .ino) — that way this "/" never competes with your dashboard's "/".
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", PORTAL_HTML);
    });

    // Save credentials and reboot
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (!request->hasParam("ssid", true)) {
        request->send(400, "text/plain", "Missing SSID");
        return;
      }
      String ssid = request->getParam("ssid", true)->value();
      String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";

      saveCredentials(ssid, pass);
      request->send(200, "text/plain", "Saved. Restarting...");

      delay(500); // let the response actually flush before rebooting
      ESP.restart();
    });

    // Common captive-portal probe endpoints, so phones/laptops pop the setup
    // page automatically instead of silently deciding "no internet, ignore".
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {       // Android
      request->redirect("/");
    });
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) { // iOS/macOS
      request->redirect("/");
    });
    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {           // Windows
      request->send_P(200, "text/plain", PORTAL_DETECT_RESPONSE);
    });

    // Catch-all: anything else while in AP mode also redirects to the setup page.
    // (Only registered in AP mode — see the .ino — so this never touches your
    // dashboard's own onNotFound, if you add one later.)
    server.onNotFound([](AsyncWebServerRequest *request) {
      request->redirect("/");
    });
  }

  bool isAPMode() {
    return apModeActive;
  }

  // Call once, right after a successful tryConnectSTA(), so the device is
  // reachable at http://<hostname>.local instead of needing you to know its IP.
  // Works out of the box on iOS/macOS (Bonjour) and most Android/Linux setups;
  // Windows 10+ generally has it too, but if a machine can't resolve it,
  // WiFi.localIP() printed over the log WebSocket (see .ino) is the fallback.
  void startMDNS(const char* hostname) {
    if (MDNS.begin(hostname)) {
      Serial.printf("mDNS started: http://%s.local\n", hostname);
    } else {
      Serial.println("mDNS failed to start");
    }
  }

} // namespace WiFiProvision

#endif