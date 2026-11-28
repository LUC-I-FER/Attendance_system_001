#pragma once

#include <Arduino.h>
#include <strings.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../student_db/student_db.h"
#include "../class_db/class_db.h"

#define MAX_CAPTURED_MACS 70

enum SystemMode {
    SESSION_MODE,
    EDIT_MODE,
    VERIFY_MODE,
    HEAL_MODE
};

class AdminLogger {
private:
    StudentDB& studentDB;
    ClassDB& classDB;

    ClassRecord activeClass;
    bool hasActiveClass;

    char capturedMacs[MAX_CAPTURED_MACS][18];
    size_t capturedCount;

    SystemMode mode;
    SemaphoreHandle_t adminMutex;
    bool enable_debug;

    void copyStr(char* dest, const char* src, size_t maxLen) {
        if (!src) {
            dest[0] = '\0';
            return;
        }
        strncpy(dest, src, maxLen - 1);
        dest[maxLen - 1] = '\0';
    }

public:
    AdminLogger(StudentDB& sdb, ClassDB& cdb, bool debug = true)
        : studentDB(sdb), classDB(cdb), hasActiveClass(false),
          capturedCount(0), mode(SESSION_MODE), enable_debug(debug) {
        adminMutex = xSemaphoreCreateMutex();
    }

    ~AdminLogger() {
        if (adminMutex) {
            vSemaphoreDelete(adminMutex);
        }
    }

    // 0. Initialize both databases and restore active class on boot
    bool begin() {
        bool sOk = studentDB.loadStudents();
        bool cOk = classDB.loadClasses();

        if (!sOk || !cOk) {
            if (enable_debug) Serial.println("[ADMIN] Database initialization failed.");
            return false;
        }

        ClassRecord* active = classDB.getActiveClass();
        if (active) {
            activeClass = *active;
            hasActiveClass = true;
            if (enable_debug) {
                Serial.printf("[ADMIN] Restored active class: %s (%s)\n",
                              activeClass.className, activeClass.classCode);
            }
        }
        return true;
    }

    // 1. Set active class (only if it exists in ClassDB)
    bool setActiveClass(const char* classCode) {
        ClassRecord* found = classDB.searchByCode(classCode);
        if (!found) {
            if (enable_debug) {
                Serial.printf("[ADMIN] Class '%s' not found in ClassDB.\n", classCode);
            }
            return false;
        }

        if (!classDB.setClassActive(classCode, true)) {
            return false;
        }

        if (xSemaphoreTake(adminMutex, portMAX_DELAY) == pdTRUE) {
            activeClass = *found;
            activeClass.isActive = true;
            hasActiveClass = true;
            xSemaphoreGive(adminMutex);
        }

        if (enable_debug) {
            Serial.printf("[ADMIN] Active class set to: %s (%s) [bit %u]\n",
                          activeClass.className, activeClass.classCode, activeClass.classBit);
        }
        return true;
    }

    // 2. Store a captured MAC address (thread-safe, ignores duplicates)
    bool storeCapturedMac(const char* macAddress) {
        if (!macAddress || macAddress[0] == '\0') return false;
        if (xSemaphoreTake(adminMutex, portMAX_DELAY) != pdTRUE) return false;

        if (capturedCount >= MAX_CAPTURED_MACS) {
            if (enable_debug) Serial.println("[ADMIN] Captured MAC buffer is full.");
            xSemaphoreGive(adminMutex);
            return false;
        }

        for (size_t i = 0; i < capturedCount; i++) {
            if (strcasecmp(capturedMacs[i], macAddress) == 0) {
                xSemaphoreGive(adminMutex);
                return false; // Already captured
            }
        }

        copyStr(capturedMacs[capturedCount], macAddress, sizeof(capturedMacs[0]));
        capturedCount++;

        if (enable_debug) {
            Serial.printf("[ADMIN] Captured MAC: %s (Total: %u)\n", macAddress, capturedCount);
        }

        xSemaphoreGive(adminMutex);
        return true;
    }

    void clearCapturedMacs() {
        if (xSemaphoreTake(adminMutex, portMAX_DELAY) == pdTRUE) {
            capturedCount = 0;
            xSemaphoreGive(adminMutex);
        }
    }

    // 3. Set & Get system mode
    void setMode(SystemMode newMode) {
        if (xSemaphoreTake(adminMutex, portMAX_DELAY) == pdTRUE) {
            mode = newMode;
            xSemaphoreGive(adminMutex);
        }
        if (enable_debug) {
            Serial.printf("[ADMIN] Mode changed to: %d\n", static_cast<int>(newMode));
        }
    }

    SystemMode getMode() const { return mode; }
    size_t getCapturedCount() const { return capturedCount; }
    bool isClassActive() const { return hasActiveClass; }
    const ClassRecord* getActiveClass() const { return hasActiveClass ? &activeClass : nullptr; }

    // 4. Verify attendance against StudentDB + activeClass bitmask
    // Returns number of valid, enrolled students found in capturedMacs
    size_t verifyAttendance() {
        if (!hasActiveClass) {
            if (enable_debug) Serial.println("[ADMIN] Cannot verify: no active class.");
            return 0;
        }

        size_t validCount = 0;
        if (xSemaphoreTake(adminMutex, portMAX_DELAY) != pdTRUE) return 0;

        for (size_t i = 0; i < capturedCount; i++) {
            Student* s = studentDB.searchByMacAddress(capturedMacs[i]);
            if (s && StudentDB::isEnrolled(s, activeClass.classBit)) {
                validCount++;
                if (enable_debug) {
                    Serial.printf("[VERIFY] PRESENT: %s (%s)\n", s->name, s->rollNumber);
                }
            } else if (enable_debug) {
                Serial.printf("[VERIFY] IGNORED: %s (unknown or not enrolled)\n", capturedMacs[i]);
            }
        }

        xSemaphoreGive(adminMutex);
        return validCount;
    }

    // 5. Add a class to ClassDB
    bool addClass(const char* className, const char* startTime,
                  const char* endTime, const char* classCode, bool isActive = false) {
        return classDB.addClass(className, startTime, endTime, classCode, isActive);
    }

    // 6. Add a student to StudentDB
    bool addStudent(const char* name, const char* rollNumber, const char* macAddress) {
        return studentDB.addStudent(name, rollNumber, macAddress);
    }

    // 7. Delete a class safely (clears classBit from all students first)
    bool deleteClass(const char* classCode) {
        ClassRecord* target = classDB.searchByCode(classCode);
        if (!target) return false;

        uint8_t bitToClear = target->classBit;
        studentDB.clearClassBitForAll(bitToClear);

        if (xSemaphoreTake(adminMutex, portMAX_DELAY) == pdTRUE) {
            if (hasActiveClass && strcasecmp(activeClass.classCode, classCode) == 0) {
                hasActiveClass = false;
            }
            xSemaphoreGive(adminMutex);
        }

        return classDB.deleteClass(classCode);
    }

    // 8. Update student data (preserves existing classMask enrollments)
    bool updateStudent(const char* rollNumber, const char* newName, const char* newMacAddress) {
        Student* existing = studentDB.searchByRollNumber(rollNumber);
        if (!existing) {
            if (enable_debug) Serial.println("[ADMIN] Student not found for update.");
            return false;
        }

        uint32_t savedMask = existing->classMask;

        if (!studentDB.deleteStudent(rollNumber)) return false;
        if (!studentDB.addStudent(newName, rollNumber, newMacAddress)) return false;

        // Restore previous class enrollments
        for (uint8_t bit = 0; bit < MAX_CLASS_BITS; bit++) {
            if (savedMask & (1UL << bit)) {
                studentDB.enrollInClass(rollNumber, bit);
            }
        }
        return true;
    }

    // 9. Update class data (matched by classCode)
    bool updateClass(const char* classCode, const char* newClassName,
                     const char* newStartTime, const char* newEndTime, bool newIsActive) {
        ClassRecord* existing = classDB.searchByCode(classCode);
        if (!existing) {
            if (enable_debug) Serial.println("[ADMIN] Class not found for update.");
            return false;
        }

        if (!classDB.deleteClass(classCode)) return false;
        bool added = classDB.addClass(newClassName, newStartTime, newEndTime, classCode, newIsActive);

        if (added && hasActiveClass && strcasecmp(activeClass.classCode, classCode) == 0) {
            ClassRecord* updated = classDB.searchByCode(classCode);
            if (updated && xSemaphoreTake(adminMutex, portMAX_DELAY) == pdTRUE) {
                activeClass = *updated;
                xSemaphoreGive(adminMutex);
            }
        }
        return added;
    }

    // 10. Link / Unlink student and class using classBit
    bool linkStudentAndClass(const char* rollNumber, const char* classCode) {
        ClassRecord* cls = classDB.searchByCode(classCode);
        if (!cls) {
            if (enable_debug) Serial.println("[ADMIN] Cannot link: class not found.");
            return false;
        }
        return studentDB.enrollInClass(rollNumber, cls->classBit);
    }

    bool unlinkStudentAndClass(const char* rollNumber, const char* classCode) {
        ClassRecord* cls = classDB.searchByCode(classCode);
        if (!cls) return false;
        return studentDB.unenrollFromClass(rollNumber, cls->classBit);
    }

    // 11. Export attendance data (stub for now)
    void exportData() {
        // TODO: Export verified attendance for activeClass to /logs/<date>_<classCode>.csv
    }
};