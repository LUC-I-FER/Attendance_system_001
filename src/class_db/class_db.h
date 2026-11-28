#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <strings.h> // For strcasecmp
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define MAX_CLASSES 32

struct ClassRecord {
    char className[32];
    char startTime[8];   // e.g., "09:00"
    char endTime[8];     // e.g., "10:00"
    char classCode[16];  // Unique key, e.g., "CS101"
    bool isActive;
    uint8_t classBit;    // Permanent bit index (0..31)
};

class ClassDB {
private:
    ClassRecord classes[MAX_CLASSES];
    size_t classCount;
    SemaphoreHandle_t dbMutex;

    const char* CLASS_FILE = "/db/classes.bin";
    const char* CLASS_TMP  = "/db/classes.tmp";
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

    // Internal unlocked search helper (call only while holding dbMutex)
    ClassRecord* findByCodeUnlocked(const char* classCode) {
        for (size_t i = 0; i < classCount; i++) {
            if (strcasecmp(classes[i].classCode, classCode) == 0) {
                return &classes[i];
            }
        }
        return nullptr;
    }

    // Atomic binary save (call only while holding dbMutex)
    bool saveClassesUnlocked() {
        File file = LittleFS.open(CLASS_TMP, FILE_WRITE);
        if (!file) {
            if (enable_debug) Serial.println("[CLASS_DB] Failed to open temp file for writing.");
            return false;
        }

        if (classCount > 0) {
            size_t bytesToWrite = classCount * sizeof(ClassRecord);
            size_t written = file.write(reinterpret_cast<const uint8_t*>(classes), bytesToWrite);
            file.close();

            if (written != bytesToWrite) {
                if (enable_debug) Serial.println("[CLASS_DB] Incomplete binary write.");
                LittleFS.remove(CLASS_TMP);
                return false;
            }
        } else {
            file.close();
        }

        LittleFS.remove(CLASS_FILE);
        return LittleFS.rename(CLASS_TMP, CLASS_FILE);
    }

    // Find lowest unused bit (0..31). Returns -1 if full.
    int allocateFreeBitUnlocked() {
        uint32_t usedMask = 0;
        for (size_t i = 0; i < classCount; i++) {
            if (classes[i].classBit < MAX_CLASSES) {
                usedMask |= (1UL << classes[i].classBit);
            }
        }

        for (uint8_t bit = 0; bit < MAX_CLASSES; bit++) {
            if ((usedMask & (1UL << bit)) == 0) {
                return bit;
            }
        }
        return -1; // No free bit available
    }

public:
    ClassDB(bool debug = true)
        : classCount(0), enable_debug(debug) {
        dbMutex = xSemaphoreCreateMutex();
    }

    ~ClassDB() {
        if (dbMutex) {
            vSemaphoreDelete(dbMutex);
        }
    }

    // 1. Load classes from LittleFS binary file into memory
    bool loadClasses() {
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return false;

        if (!LittleFS.begin(true)) {
            if (enable_debug) Serial.println("[CLASS_DB] LittleFS mount failed.");
            xSemaphoreGive(dbMutex);
            return false;
        }

        if (!LittleFS.exists("/db")) LittleFS.mkdir("/db");

        classCount = 0;

        if (!LittleFS.exists(CLASS_FILE)) {
            if (enable_debug) Serial.println("[CLASS_DB] No classes.bin found, starting empty.");
            xSemaphoreGive(dbMutex);
            return true;
        }

        File file = LittleFS.open(CLASS_FILE, FILE_READ);
        if (!file) {
            xSemaphoreGive(dbMutex);
            return false;
        }

        size_t fileSize = file.size();
        size_t recordsInFile = fileSize / sizeof(ClassRecord);
        if (recordsInFile > MAX_CLASSES) recordsInFile = MAX_CLASSES;

        if (recordsInFile > 0) {
            size_t bytesToRead = recordsInFile * sizeof(ClassRecord);
            size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(classes), bytesToRead);
            classCount = bytesRead / sizeof(ClassRecord);
        }

        file.close();

        if (enable_debug) {
            Serial.printf("[CLASS_DB] Loaded %u classes from %s\n", classCount, CLASS_FILE);
        }

        xSemaphoreGive(dbMutex);
        return true;
    }

    // 2. Add a class (prevents duplicates and overflow, assigns a permanent classBit 0..31)
    bool addClass(const char* className, const char* startTime,
                  const char* endTime, const char* classCode, bool isActive = false) {
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return false;

        if (classCount >= MAX_CLASSES) {
            if (enable_debug) Serial.println("[CLASS_DB] Database is full.");
            xSemaphoreGive(dbMutex);
            return false;
        }

        if (findByCodeUnlocked(classCode) != nullptr) {
            if (enable_debug) Serial.println("[CLASS_DB] Duplicate classCode.");
            xSemaphoreGive(dbMutex);
            return false;
        }

        int freeBit = allocateFreeBitUnlocked();
        if (freeBit < 0) {
            if (enable_debug) Serial.println("[CLASS_DB] No free classBit available.");
            xSemaphoreGive(dbMutex);
            return false;
        }

        if (isActive) {
            for (size_t i = 0; i < classCount; i++) {
                classes[i].isActive = false;
            }
        }

        ClassRecord& c = classes[classCount];
        copyStr(c.className, className, sizeof(c.className));
        copyStr(c.startTime, startTime, sizeof(c.startTime));
        copyStr(c.endTime, endTime, sizeof(c.endTime));
        copyStr(c.classCode, classCode, sizeof(c.classCode));
        c.isActive = isActive;
        c.classBit = static_cast<uint8_t>(freeBit);
        classCount++;

        bool saved = saveClassesUnlocked();
        xSemaphoreGive(dbMutex);
        return saved;
    }

    // 3. Delete a class by classCode (O(1) swap-with-last removal)
    bool deleteClass(const char* classCode) {
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return false;

        for (size_t i = 0; i < classCount; i++) {
            if (strcasecmp(classes[i].classCode, classCode) == 0) {
                if (i < classCount - 1) {
                    classes[i] = classes[classCount - 1];
                }
                classCount--;

                bool saved = saveClassesUnlocked();
                xSemaphoreGive(dbMutex);
                return saved;
            }
        }

        xSemaphoreGive(dbMutex);
        return false;
    }

    // 4. Search class by classCode (returns nullptr if not found)
    ClassRecord* searchByCode(const char* classCode) {
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return nullptr;

        ClassRecord* found = findByCodeUnlocked(classCode);
        xSemaphoreGive(dbMutex);
        return found;
    }

    // 5. Search class by className (returns nullptr if not found)
    ClassRecord* searchByName(const char* className) {
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return nullptr;

        ClassRecord* found = nullptr;
        for (size_t i = 0; i < classCount; i++) {
            if (strcasecmp(classes[i].className, className) == 0) {
                found = &classes[i];
                break;
            }
        }

        xSemaphoreGive(dbMutex);
        return found;
    }

    // 6. Set a class active/inactive (deactivates others if setting to true)
    bool setClassActive(const char* classCode, bool active) {
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return false;

        ClassRecord* target = findByCodeUnlocked(classCode);
        if (!target) {
            xSemaphoreGive(dbMutex);
            return false;
        }

        if (active) {
            for (size_t i = 0; i < classCount; i++) {
                classes[i].isActive = false;
            }
        }
        target->isActive = active;

        bool saved = saveClassesUnlocked();
        xSemaphoreGive(dbMutex);
        return saved;
    }

    // 7. Get the currently active class (returns nullptr if none active)
    ClassRecord* getActiveClass() {
        if (xSemaphoreTake(dbMutex, portMAX_DELAY) != pdTRUE) return nullptr;

        ClassRecord* active = nullptr;
        for (size_t i = 0; i < classCount; i++) {
            if (classes[i].isActive) {
                active = &classes[i];
                break;
            }
        }

        xSemaphoreGive(dbMutex);
        return active;
    }
};