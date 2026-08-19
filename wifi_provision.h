#ifndef WIFI_PROVISION_H
#define WIFI_PROVISION_H

#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>

#include "wifi_portal_page.h"

namespace WiFiProvision {

  // ==========================================================
  // STATE
  // ==========================================================

  Preferences prefs;
  DNSServer dnsServer;
  bool apModeActive = false;

  const byte DNS_PORT = 53;
  const char* AP_NAME = "Screwdriver-Setup";
  const IPAddress AP_IP(192, 168, 4, 1);

  // ==========================================================
  // SAVED CREDENTIALS
  // ==========================================================

  bool hasSavedCredentials() {
    prefs.begin("wifi", true);
    bool has = prefs.isKey("ssid");
    prefs.end();
    return has;
  }

  // ==========================================================
  // SAVE
  // ==========================================================

  void saveCredentials(const String& ssid, const String& pass) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
  }

  // ==========================================================
  // CLEAR
  // ==========================================================

  void clearCredentials() {
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();
  }

  // ==========================================================
  // STA CONNECTION
  // ==========================================================

  bool tryConnectSTA(unsigned long timeoutMs = 10000) {
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();

    if (ssid.length() == 0) {
      return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
      delay(250);
    }

    return (WiFi.status() == WL_CONNECTED);
  }

  // ==========================================================
  // AP SETUP MODE
  // ==========================================================

  void startAPMode() {
    apModeActive = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_NAME);
    dnsServer.start(DNS_PORT, "*", AP_IP);

    Serial.print("Setup AP started. IP address: ");
    Serial.println(AP_IP);
  }

  // ==========================================================
  // LOOP
  // ==========================================================

  void handleLoop() {
    if (apModeActive) {
      dnsServer.processNextRequest();
    }
  }

  // ==========================================================
  // PORTAL ROUTES
  // ==========================================================

  void setupPortalRoutes(AsyncWebServer &server) {
    
    // --------------------------------------------------------
    // Setup page
    // --------------------------------------------------------
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send_P(200, "text/html", PORTAL_HTML);
    });

    // --------------------------------------------------------
    // Save credentials
    // --------------------------------------------------------
    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (!request->hasParam("ssid", true)) {
        request->send(400, "text/plain", "Missing SSID");
        return;
      }

      String ssid = request->getParam("ssid", true)->value();
      String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";

      saveCredentials(ssid, pass);

      request->send(200, "text/plain", "Saved. Restarting...");
      
      // Give the async web server a brief moment to queue the response 
      // before the hard reset interrupts the TCP connection.
      delay(100); 
      ESP.restart();
    });

    // --------------------------------------------------------
    // Captive portal endpoints
    // --------------------------------------------------------
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->redirect("/");
    });

    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->redirect("/");
    });

    server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send_P(200, "text/plain", PORTAL_DETECT_RESPONSE);
    });

    // --------------------------------------------------------
    // Catch-all
    // --------------------------------------------------------
    server.onNotFound([](AsyncWebServerRequest *request) {
      request->redirect("/");
    });
    
  } // End of setupPortalRoutes

  // ==========================================================
  // AP STATE
  // ==========================================================

  bool isAPMode() {
    return apModeActive;
  }

  // ==========================================================
  // mDNS
  // ==========================================================

  void startMDNS(const char* hostname) {
    if (MDNS.begin(hostname)) {
      Serial.printf("mDNS started: http://%s.local\n", hostname);
    } else {
      Serial.println("mDNS failed to start");
    }
  }

} // namespace WiFiProvision

#endif