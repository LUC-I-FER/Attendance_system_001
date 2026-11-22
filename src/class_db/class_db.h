#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>

struct ClassRecord {
    String className;
    String startTime;   // e.g., "09:00"
    String endTime;     // e.g., "10:00"
    String classCode;   // Unique key, e.g., "CS101"
    bool isActive = false;
};

class ClassDB {
private:
    std::vector<ClassRecord> classes;
    const char* CLASS_FILE = "/db/classes.json";
    const char* CLASS_TMP  = "/db/classes.tmp";
    bool enable_debug = true;

    bool saveClasses() {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (const auto& c : classes) {
            JsonObject obj = arr.add<JsonObject>();
            obj["className"] = c.className;
            obj["startTime"] = c.startTime;
            obj["endTime"]   = c.endTime;
            obj["classCode"] = c.classCode;
            obj["isActive"]  = c.isActive;
        }

        File file = LittleFS.open(CLASS_TMP, FILE_WRITE);
        if (!file) {
            if (enable_debug) Serial.println("[CLASS_DB] Failed to open temp file for writing.");
            return false;
        }

        serializeJson(doc, file);
        file.close();

        LittleFS.remove(CLASS_FILE);
        return LittleFS.rename(CLASS_TMP, CLASS_FILE);
    }

public:
    // 1. Load classes from LittleFS into memory
    bool loadClasses() {
        if (!LittleFS.begin(true)) {
            if (enable_debug) Serial.println("[CLASS_DB] LittleFS mount failed.");
            return false;
        }

        if (!LittleFS.exists("/db")) LittleFS.mkdir("/db");

        classes.clear();
        classes.reserve(20);

        if (!LittleFS.exists(CLASS_FILE)) {
            if (enable_debug) Serial.println("[CLASS_DB] No classes.json found, starting empty.");
            return true;
        }

        File file = LittleFS.open(CLASS_FILE, FILE_READ);
        if (!file) return false;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, file);
        file.close();

        if (err) {
            if (enable_debug) {
                Serial.print("[CLASS_DB] JSON parse failed: ");
                Serial.println(err.c_str());
            }
            return false;
        }

        for (JsonObject obj : doc.as<JsonArray>()) {
            ClassRecord c;
            c.className = obj["className"] | "";
            c.startTime = obj["startTime"] | "";
            c.endTime   = obj["endTime"] | "";
            c.classCode = obj["classCode"] | "";
            c.isActive  = obj["isActive"] | false;
            classes.push_back(c);
        }

        if (enable_debug) {
            Serial.printf("[CLASS_DB] Loaded %u classes from %s\n", classes.size(), CLASS_FILE);
        }
        return true;
    }

    // 2. Add a class (prevents duplicate classCode)
    bool addClass(const String& className, const String& startTime,
                  const String& endTime, const String& classCode, bool isActive = false) {
        if (searchByCode(classCode) != nullptr) {
            if (enable_debug) Serial.println("[CLASS_DB] Duplicate classCode.");
            return false;
        }

        ClassRecord c;
        c.className = className;
        c.startTime = startTime;
        c.endTime   = endTime;
        c.classCode = classCode;
        c.isActive  = isActive;
        classes.push_back(c);
        return saveClasses();
    }

    // 3. Delete a class by classCode
    bool deleteClass(const String& classCode) {
        for (auto it = classes.begin(); it != classes.end(); ++it) {
            if (it->classCode.equalsIgnoreCase(classCode)) {
                classes.erase(it);
                return saveClasses();
            }
        }
        return false;
    }

    // 4. Search class by classCode (returns nullptr if not found)
    ClassRecord* searchByCode(const String& classCode) {
        for (auto& c : classes) {
            if (c.classCode.equalsIgnoreCase(classCode)) return &c;
        }
        return nullptr;
    }

    // 5. Search class by className (returns nullptr if not found)
    ClassRecord* searchByName(const String& className) {
        for (auto& c : classes) {
            if (c.className.equalsIgnoreCase(className)) return &c;
        }
        return nullptr;
    }

    // 6. Set a class active/inactive (deactivates others if setting to true)
    bool setClassActive(const String& classCode, bool active) {
        ClassRecord* target = searchByCode(classCode);
        if (!target) return false;

        if (active) {
            for (auto& c : classes) c.isActive = false;
        }
        target->isActive = active;
        return saveClasses();
    }

    // 7. Get the currently active class (returns nullptr if none active)
    ClassRecord* getActiveClass() {
        for (auto& c : classes) {
            if (c.isActive) return &c;
        }
        return nullptr;
    }
};  