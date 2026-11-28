#include <Arduino.h>
#include <LittleFS.h>
#include "student_db/student_db.h"
#include "class_db/class_db.h"
#include "admin_logger/admin_logger.h"
#include "wifi_manager/wifi_manager.h"

// Core system modules
StudentDB   studentDB(true);
ClassDB     classDB(true);
AdminLogger adminLogger(studentDB, classDB, true);
WifiUtility wifiUtil(80, true);

void setup() {
    Serial.begin(115200);
    delay(1000);

    // 0. Hardware Check (16 MB Flash + 8 MB PSRAM + LittleFS)
    Serial.println("\n--- System Hardware Check ---");
    Serial.printf("[SYS] Flash Size: %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("[SYS] PSRAM Found: %s | Total: %u KB | Free: %u KB\n",
                  psramFound() ? "YES" : "NO",
                  ESP.getPsramSize() / 1024,
                  ESP.getFreePsram() / 1024);

    if (LittleFS.begin(true)) {
        Serial.printf("[SYS] LittleFS Total: %u KB | Used: %u KB\n",
                      LittleFS.totalBytes() / 1024,
                      LittleFS.usedBytes() / 1024);
    } else {
        Serial.println("[SYS] LittleFS mount failed!");
    }
    Serial.println("-----------------------------\n");

    // 1. Initialize StudentDB (PSRAM), ClassDB (SRAM), and restore active class
    if (!adminLogger.begin()) {
        Serial.println("[MAIN] AdminLogger initialization failed!");
    }

    // 2. Bind AdminLogger to WifiUtility
    wifiUtil.bindAdminLogger(&adminLogger);

    // 3. Link HTML pages from LittleFS to URL routes
    wifiUtil.addRoute("/", "/index.html");
    wifiUtil.addRoute("/admin", "/admin.html");
    wifiUtil.addRoute("/verify", "/verify.html");

    // 4. Start Open Soft-AP and Web Server
    wifiUtil.begin("College_Attendance_AP");
}

void loop() {
    // Event-driven: Wi-Fi and AsyncWebServer run on their own FreeRTOS tasks
    wifiUtil.loop();  // Process DNS and auto-disconnect timers
}