#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <strings.h> // For strcasecmp
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define MAX_STUDENTS 1000

struct Student {
    char name[32];
    char rollNumber[10];
    char macAddress[18]; // "AA:BB:CC:DD:EE:FF" + null terminator
};

class StudentDB {
private:
    Student* students;
    size_t studentCount;
    SemaphoreHandle_t dbMutex;

    const char* STUDENT_FILE = "/db/students.bin";
    const char* STUDENT_TMP  = "/db/students.tmp";
    bool enable_debug;

    // Helper: Safe null-terminated string copy
    void copyStr(char* dest, const char* src, size_t maxLen) {
        if (!src) {
            dest[0] = '\0';
            return;
        }
        strncpy(dest, src, maxLen - 1);
        dest[maxLen - 1] = '\0';
    }

    // Internal unlocked search helpers (call only while holding dbMutex)
    Student* findByRollUnlocked(const char* rollNumber) {
        for (size_t i = 0; i < studentCount; i++) {
            if (strcasecmp(students[i].rollNumber, rollNumber) == 0) {
                return &students[i];
            }
        }
        return nullptr;
    }

    Student* findByMacUnlocked(const char* macAddress) {
        for (size_t i = 0; i < studentCount; i++) {
            if (strcasecmp(students[i].macAddress, macAddress) == 0) {
                return &students[i];
            }
        }
        return nullptr;
    }

    // Atomic binary save (call only while holding dbMutex)
    bool saveStudentsUnlocked() {
        File file = LittleFS.open(STUDENT_TMP, FILE_WRITE);
        if (!file) {
            if (enable_debug) Serial.println("[STUDENT_DB] Failed to open temp file for writing.");
            return false;
        }

        if (studentCount > 0) {
            size_t bytesToWrite = studentCount * sizeof(Student);
            size_t written = file.write(reinterpret_cast<const uint8_t*>(students), bytesToWrite);
            file.close();

            if (written != bytesToWrite) {
                if (enable_debug) Serial.println("[STUDENT_DB] Incomplete binary write.");
                LittleFS.remove(STUDENT_TMP);
                return false;
            }
        } else {
            file.close();
        }

        LittleFS.remove(STUDENT_FILE);
        return LittleFS.rename(STUDENT_TMP, STUDENT_FILE);
    }

public:
    StudentDB(bool debug = true)
        : students(nullptr), studentCount(0), enable_debug(debug) {
        dbMutex = xSemaphoreCreateMutex();
    }

    ~StudentDB() {
        if (students) {
            free(students);
            students = nullptr;
        }
        if (dbMutex) {
            vSemaphoreDelete(dbMutex);
        }
    }

    // 1. Allocate PSRAM buffer and load students from LittleFS binary file
    bool loadStudents() {
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return false;

        if (!students) {
            size_t totalBytes = MAX_STUDENTS * sizeof(Student);
            if (psramFound()) {
                students = static_cast<Student*>(ps_malloc(totalBytes));
                if (enable_debug) Serial.printf("[STUDENT_DB] Allocated %u bytes in PSRAM.\n", totalBytes);
            } else {
                students = static_cast<Student*>(malloc(totalBytes));
                if (enable_debug) Serial.printf("[STUDENT_DB] PSRAM not found; allocated %u bytes in SRAM.\n", totalBytes);
            }

            if (!students) {
                if (enable_debug) Serial.println("[STUDENT_DB] Memory allocation failed!");
                xSemaphoreGive(dbMutex);
                return false;
            }
        }

        if (!LittleFS.begin(true)) {
            if (enable_debug) Serial.println("[STUDENT_DB] LittleFS mount failed.");
            xSemaphoreGive(dbMutex);
            return false;
        }

        if (!LittleFS.exists("/db")) LittleFS.mkdir("/db");

        studentCount = 0;

        if (!LittleFS.exists(STUDENT_FILE)) {
            if (enable_debug) Serial.println("[STUDENT_DB] No students.bin found, starting empty.");
            xSemaphoreGive(dbMutex);
            return true;
        }

        File file = LittleFS.open(STUDENT_FILE, FILE_READ);
        if (!file) {
            xSemaphoreGive(dbMutex);
            return false;
        }

        size_t fileSize = file.size();
        size_t recordsInFile = fileSize / sizeof(Student);
        if (recordsInFile > MAX_STUDENTS) recordsInFile = MAX_STUDENTS;

        if (recordsInFile > 0) {
            size_t bytesToRead = recordsInFile * sizeof(Student);
            size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(students), bytesToRead);
            studentCount = bytesRead / sizeof(Student);
        }

        file.close();

        if (enable_debug) {
            Serial.printf("[STUDENT_DB] Loaded %u students from %s\n", studentCount, STUDENT_FILE);
        }

        xSemaphoreGive(dbMutex);
        return true;
    }

    // 2. Add a student (prevents duplicates and overflow)
    bool addStudent(const char* name, const char* rollNumber, const char* macAddress) {
        if (!students) return false;
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return false;

        if (studentCount >= MAX_STUDENTS) {
            if (enable_debug) Serial.println("[STUDENT_DB] Database is full.");
            xSemaphoreGive(dbMutex);
            return false;
        }

        if (findByRollUnlocked(rollNumber) != nullptr || findByMacUnlocked(macAddress) != nullptr) {
            if (enable_debug) Serial.println("[STUDENT_DB] Duplicate rollNumber or macAddress.");
            xSemaphoreGive(dbMutex);
            return false;
        }

        Student& s = students[studentCount];
        copyStr(s.name, name, sizeof(s.name));
        copyStr(s.rollNumber, rollNumber, sizeof(s.rollNumber));
        copyStr(s.macAddress, macAddress, sizeof(s.macAddress));
        studentCount++;

        bool saved = saveStudentsUnlocked();
        xSemaphoreGive(dbMutex);
        return saved;
    }

    // 3. Delete a student by roll number (O(1) swap-with-last removal)
    bool deleteStudent(const char* rollNumber) {
        if (!students) return false;
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return false;

        for (size_t i = 0; i < studentCount; i++) {
            if (strcasecmp(students[i].rollNumber, rollNumber) == 0) {
                // Swap with the last element to avoid shifting hundreds of records
                if (i < studentCount - 1) {
                    students[i] = students[studentCount - 1];
                }
                studentCount--;

                bool saved = saveStudentsUnlocked();
                xSemaphoreGive(dbMutex);
                return saved;
            }
        }

        xSemaphoreGive(dbMutex);
        return false;
    }

    // 4. Search student by name
    Student* searchByName(const char* name) {
        if (!students) return nullptr;
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return nullptr;

        Student* found = nullptr;
        for (size_t i = 0; i < studentCount; i++) {
            if (strcasecmp(students[i].name, name) == 0) {
                found = &students[i];
                break;
            }
        }

        xSemaphoreGive(dbMutex);
        return found;
    }

    // 5. Search student by roll number
    Student* searchByRollNumber(const char* rollNumber) {
        if (!students) return nullptr;
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return nullptr;

        Student* found = findByRollUnlocked(rollNumber);
        xSemaphoreGive(dbMutex);
        return found;
    }

    // 6. Search student by MAC address
    Student* searchByMacAddress(const char* macAddress) {
        if (!students) return nullptr;
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return nullptr;

        Student* found = findByMacUnlocked(macAddress);
        xSemaphoreGive(dbMutex);
        return found;
    }
};