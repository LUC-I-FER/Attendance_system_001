#include <Arduino.h>
#include "wifi_manager/wifi_manager.h"

#define MAX_MACS 70

// External array to hold captured MAC addresses
char capturedMacs[MAX_MACS][18];
size_t capturedMacCount = 0;

WifiUtility wifiUtil(80);

void setup() {
    Serial.begin(115200);
    delay(1000);

    // 1. Bind the external MAC array
    wifiUtil.bindMacArray(capturedMacs, &capturedMacCount, MAX_MACS);

    // 2. Link HTML pages from LittleFS to URL routes
    wifiUtil.addRoute("/", "/index.html");
    wifiUtil.addRoute("/admin", "/admin.html");
    wifiUtil.addRoute("/verify", "/verify.html");

    // 3. Start Soft-AP and web server
    wifiUtil.begin("College_Attendance_AP");
}

void loop() {
    // Event-driven: no polling needed here
}