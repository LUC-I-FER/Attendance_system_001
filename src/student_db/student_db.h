#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>

struct Student {
    String name;
    String rollNumber;
    String macAddress;
};

class StudentDB {
private:
    std::vector<Student> students;
    const char* STUDENT_FILE = "/db/student.json";
    const char* STUDENT_TMP  = "/db/students.tmp";
    bool enable_debug = true;

    bool saveStudents() {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (const auto& s : students) {
            JsonObject obj = arr.add<JsonObject>();
            obj["name"]       = s.name;
            obj["rollNumber"] = s.rollNumber;
            obj["macAddress"] = s.macAddress;
        }

        File file = LittleFS.open(STUDENT_TMP, FILE_WRITE);
        if (!file) {
            if (enable_debug) Serial.println("[STUDENT_DB] Failed to open temp file for writing.");
            return false;
        }

        serializeJson(doc, file);
        file.close();

        LittleFS.remove(STUDENT_FILE);
        return LittleFS.rename(STUDENT_TMP, STUDENT_FILE);
    }

public:
    // 1. Load students from LittleFS into memory
    bool loadStudents() {
        if (!LittleFS.begin(true)) {
            if (enable_debug) Serial.println("[STUDENT_DB] LittleFS mount failed.");
            return false;
        }

        if (!LittleFS.exists("/db")) LittleFS.mkdir("/db");

        students.clear();
        students.reserve(70);

        if (!LittleFS.exists(STUDENT_FILE)) {
            if (enable_debug) Serial.println("[STUDENT_DB] No student.json found, starting empty.");
            return true;
        }

        File file = LittleFS.open(STUDENT_FILE, FILE_READ);
        if (!file) return false;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, file);
        file.close();

        if (err) {
            if (enable_debug) {
                Serial.print("[STUDENT_DB] JSON parse failed: ");
                Serial.println(err.c_str());
            }
            return false;
        }

        for (JsonObject obj : doc.as<JsonArray>()) {
            Student s;
            s.name       = obj["name"] | "";
            s.rollNumber = obj["rollNumber"] | "";
            s.macAddress = obj["macAddress"] | "";
            students.push_back(s);
        }

        if (enable_debug) {
            Serial.printf("[STUDENT_DB] Loaded %u students from %s\n", students.size(), STUDENT_FILE);
        }
        return true;
    }

    // 2. Add a student (prevents duplicate rollNumber or macAddress)
    bool addStudent(const String& name, const String& rollNumber, const String& macAddress) {
        if (searchByRollNumber(rollNumber) != nullptr || searchByMacAddress(macAddress) != nullptr) {
            if (enable_debug) Serial.println("[STUDENT_DB] Duplicate rollNumber or macAddress.");
            return false;
        }

        students.push_back({name, rollNumber, macAddress});
        return saveStudents();
    }

    // 3. Delete a student by roll number
    bool deleteStudent(const String& rollNumber) {
        for (auto it = students.begin(); it != students.end(); ++it) {
            if (it->rollNumber.equalsIgnoreCase(rollNumber)) {
                students.erase(it);
                return saveStudents();
            }
        }
        return false;
    }

    // 4. Search student by name (returns nullptr if not found)
    Student* searchByName(const String& name) {
        for (auto& s : students) {
            if (s.name.equalsIgnoreCase(name)) return &s;
        }
        return nullptr;
    }

    // 5. Search student by roll number (returns nullptr if not found)
    Student* searchByRollNumber(const String& rollNumber) {
        for (auto& s : students) {
            if (s.rollNumber.equalsIgnoreCase(rollNumber)) return &s;
        }
        return nullptr;
    }

    // 6. Search student by MAC address (returns nullptr if not found)
    Student* searchByMacAddress(const String& macAddress) {
        for (auto& s : students) {
            if (s.macAddress.equalsIgnoreCase(macAddress)) return &s;
        }
        return nullptr;
    }
};