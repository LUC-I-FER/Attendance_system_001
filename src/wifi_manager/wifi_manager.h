#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>
#include "../admin_logger/admin_logger.h"

#define MAX_AP_STATIONS 10

struct PendingKick {
    uint16_t aid;
    unsigned long kickAtMs;
    bool active;
};

class WifiUtility {
private:
    AsyncWebServer server;
    DNSServer dnsServer;
    AdminLogger* adminLogger;
    bool enable_debug;

    unsigned long disconnectDelayMs;
    PendingKick kickQueue[MAX_AP_STATIONS];
    portMUX_TYPE kickMux = portMUX_INITIALIZER_UNLOCKED;

    // Schedule a connected station for auto-disconnect
    void scheduleKick(uint16_t aid) {
        if (disconnectDelayMs == 0) return;

        unsigned long targetTime = millis() + disconnectDelayMs;
        portENTER_CRITICAL(&kickMux);
        for (size_t i = 0; i < MAX_AP_STATIONS; i++) {
            if (!kickQueue[i].active || kickQueue[i].aid == aid) {
                kickQueue[i].aid      = aid;
                kickQueue[i].kickAtMs = targetTime;
                kickQueue[i].active   = true;
                break;
            }
        }
        portEXIT_CRITICAL(&kickMux);
    }

    // Clear a station from the queue if it disconnected on its own
    void cancelKick(uint16_t aid) {
        portENTER_CRITICAL(&kickMux);
        for (size_t i = 0; i < MAX_AP_STATIONS; i++) {
            if (kickQueue[i].active && kickQueue[i].aid == aid) {
                kickQueue[i].active = false;
                break;
            }
        }
        portEXIT_CRITICAL(&kickMux);
    }

    // Event-driven MAC capture -> forwards directly to AdminLogger
    void handleStationConnected(const uint8_t* mac, uint16_t aid) {
        if (!adminLogger) return;

        // Only capture MACs and auto-disconnect during SESSION_MODE
        if (adminLogger->getMode() != SESSION_MODE) {
            if (enable_debug) Serial.println("[WIFI] Device connected (Admin/Non-session mode; staying connected).");
            return;
        }

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        adminLogger->storeCapturedMac(macStr);
        scheduleKick(aid);
    }

public:
    WifiUtility(uint16_t port = 80, bool debug = true, unsigned long autoDisconnectMs = 4000)
        : server(port), adminLogger(nullptr), enable_debug(debug),
          disconnectDelayMs(autoDisconnectMs) {
        memset(kickQueue, 0, sizeof(kickQueue));
    }

    // 1. Bind AdminLogger as the controller for captured MACs
    void bindAdminLogger(AdminLogger* logger) {
        adminLogger = logger;
    }

    // Optional: Adjust auto-disconnect delay at runtime (0 = disabled)
    void setAutoDisconnectDelay(unsigned long ms) {
        disconnectDelayMs = ms;
    }

    // 2. Router: Link a URL path to an HTML file stored in LittleFS
    void addRoute(const char* uri, const char* htmlFilePath) {
        String path = htmlFilePath;
        server.on(uri, HTTP_GET, [path](AsyncWebServerRequest* request) {
            if (LittleFS.exists(path)) {
                request->send(LittleFS, path, "text/html");
            } else {
                request->send(404, "text/plain", "Page not found: " + path);
            }
        });

        if (enable_debug) {
            Serial.printf("[WIFI] Route linked: %s -> %s\n", uri, htmlFilePath);
        }
    }

    AsyncWebServer& getServer() {
        return server;
    }

    // 3. Start Open Soft-AP, Captive Portal DNS, MAC capture, and Web Server
    bool begin(const char* ssid, const char* password = nullptr, int maxConnections = MAX_AP_STATIONS) {
        if (!LittleFS.begin(true)) {
            if (enable_debug) Serial.println("[WIFI] LittleFS mount failed.");
            return false;
        }

        WiFi.mode(WIFI_AP);

        WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
            if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
                this->handleStationConnected(info.wifi_ap_staconnected.mac,
                                             info.wifi_ap_staconnected.aid);
            } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
                this->cancelKick(info.wifi_ap_stadisconnected.aid);
            }
        });

        bool apOk = WiFi.softAP(ssid, password, 1, 0, maxConnections);
        if (!apOk) {
            if (enable_debug) Serial.println("[WIFI] Failed to start Soft-AP.");
            return false;
        }

        // Start DNS server: resolve all domains (*) to the ESP32-S3 AP IP
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(53, "*", WiFi.softAPIP());

        // Captive portal catch-all: redirect OS probes (/generate_204, /hotspot-detect.html, etc.) to "/"
        server.onNotFound([](AsyncWebServerRequest* request) {
            request->redirect("http://" + WiFi.softAPIP().toString() + "/");
        });

        server.begin();

        if (enable_debug) {
            Serial.printf("[WIFI] AP + Captive Portal Started: %s | IP: %s\n",
                          ssid, WiFi.softAPIP().toString().c_str());
        }
        return true;
    }

    // 4. Call this inside Arduino loop() to process DNS and auto-disconnect timers
    void loop() {
        dnsServer.processNextRequest();

        if (!adminLogger || adminLogger->getMode() != SESSION_MODE) return;

        unsigned long now = millis();
        uint16_t aidToKick = 0;

        portENTER_CRITICAL(&kickMux);
        for (size_t i = 0; i < MAX_AP_STATIONS; i++) {
            if (kickQueue[i].active && static_cast<long>(now - kickQueue[i].kickAtMs) >= 0) {
                aidToKick = kickQueue[i].aid;
                kickQueue[i].active = false;
                break;
            }
        }
        portEXIT_CRITICAL(&kickMux);

        if (aidToKick != 0) {
            esp_wifi_deauth_sta(aidToKick);
            if (enable_debug) {
                Serial.printf("[WIFI] Auto-disconnected station AID %u to free AP slot.\n", aidToKick);
            }
        }
    }
};