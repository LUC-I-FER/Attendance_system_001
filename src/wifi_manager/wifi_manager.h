#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <strings.h>

class WifiUtility {
private:
    AsyncWebServer server;
    bool enable_debug;

    // Pointers to the external MAC array
    char (*extMacArray)[18];
    size_t* extMacCount;
    size_t extMacCapacity;

    // Event-driven MAC capture (fires automatically when a phone connects to the AP)
    void handleStationConnected(const uint8_t* mac) {
        if (!extMacArray || !extMacCount || extMacCapacity == 0) return;

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        // Prevent duplicates
        for (size_t i = 0; i < *extMacCount; i++) {
            if (strcasecmp(extMacArray[i], macStr) == 0) {
                if (enable_debug) Serial.printf("[WIFI] Duplicate MAC ignored: %s\n", macStr);
                return;
            }
        }

        // Store in external array if space is available
        if (*extMacCount < extMacCapacity) {
            strncpy(extMacArray[*extMacCount], macStr, 17);
            extMacArray[*extMacCount][17] = '\0';
            (*extMacCount)++;

            if (enable_debug) {
                Serial.printf("[WIFI] Captured MAC: %s (Total: %u)\n", macStr, *extMacCount);
            }
        } else if (enable_debug) {
            Serial.println("[WIFI] External MAC array is full!");
        }
    }

public:
    WifiUtility(uint16_t port = 80, bool debug = true)
        : server(port), enable_debug(debug),
          extMacArray(nullptr), extMacCount(nullptr), extMacCapacity(0) {}

    // 1. Bind an external fixed char array to store captured MACs
    void bindMacArray(char (*macBuffer)[18], size_t* countPtr, size_t maxCapacity) {
        extMacArray    = macBuffer;
        extMacCount    = countPtr;
        extMacCapacity = maxCapacity;
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

    // 3. Start Soft-AP, attach MAC capture event, and launch web server
    bool begin(const char* ssid, const char* password = nullptr, int maxConnections = 10) {
        if (!LittleFS.begin(true)) {
            if (enable_debug) Serial.println("[WIFI] LittleFS mount failed.");
            return false;
        }

        WiFi.mode(WIFI_AP);

        // Register event handler for incoming connections
        WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
            if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
                this->handleStationConnected(info.wifi_ap_staconnected.mac);
            }
        });

        bool apOk = WiFi.softAP(ssid, password, 1, 0, maxConnections);
        if (!apOk) {
            if (enable_debug) Serial.println("[WIFI] Failed to start Soft-AP.");
            return false;
        }

        server.begin();

        if (enable_debug) {
            Serial.printf("[WIFI] AP Started: %s | IP: %s\n",
                          ssid, WiFi.softAPIP().toString().c_str());
        }
        return true;
    }
};