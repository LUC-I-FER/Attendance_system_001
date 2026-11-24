#pragma once

#include <Arduino.h>
#include <vector>
#include "../student_db/student_db.h"
#include "../class_db/class_db.h"

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
        std::vector<String> capturedMacs;
        SystemMode mode;
        bool enable_debug;

    public:
        AdminLogger(StudentDB& sdb, ClassDB& cdb, bool debug = true)
            : studentDB(sdb), classDB(cdb), hasActiveClass(false),
              mode(SESSION_MODE), enable_debug(debug) {
            capturedMacs.reserve(70);
        }

        // 1. Set active class (only if it exists in ClassDB)
        bool setActiveClass(const String& classCode) {
            ClassRecord* found = classDB.searchByCode(classCode);
            if (!found) {
                if (enable_debug) {
                    Serial.printf("[ADMIN] Class '%s' not found in ClassDB.\n", classCode.c_str());
                }
                return false;
            }

            if (!classDB.setClassActive(classCode, true)) {
                return false;
            }

            activeClass = *found;
            activeClass.isActive = true;
            hasActiveClass = true;

            if (enable_debug) {
                Serial.printf("[ADMIN] Active class set to: %s (%s)\n", activeClass.className.c_str(), activeClass.classCode.c_str());
            }
            return true;
        }

        // 2. Store a captured MAC address in the array (ignores duplicates)
        bool storeCapturedMac(const String& macAddress) {
            for (const auto& mac : capturedMacs) {
                if (mac.equalsIgnoreCase(macAddress)) {
                    return false; // Already captured
                }
            }
            capturedMacs.push_back(macAddress);
            if (enable_debug) {
                Serial.printf("[ADMIN] Captured MAC: %s (Total: %u)\n", macAddress.c_str(), capturedMacs.size());
            }
            return true;
        }

        // 3. Set system mode
        void setMode(SystemMode newMode) {
            mode = newMode;
            if (enable_debug) {
                Serial.printf("[ADMIN] Mode changed to: %d\n", static_cast<int>(mode));
            }
        }

        // 4. Verify attendance (stub for now)
        void verifyAttendance() {
            // TODO: Match capturedMacs against StudentDB and activeClass
        }

        // 5. Add a class to ClassDB
        bool addClass(const String& className, const String& startTime,
                      const String& endTime, const String& classCode, bool isActive = false) {
            return classDB.addClass(className, startTime, endTime, classCode, isActive);
        }

        // 6. Add a student to StudentDB
        bool addStudent(const String& name, const String& rollNumber, const String& macAddress) {
            return studentDB.addStudent(name, rollNumber, macAddress);
        }

        // 7. Delete a class from ClassDB
        bool deleteClass(const String& classCode) {
            if (hasActiveClass && activeClass.classCode.equalsIgnoreCase(classCode)) {
                hasActiveClass = false;
            }
            return classDB.deleteClass(classCode);
        }

        // 8. Update student data (matched by rollNumber)
        bool updateStudent(const String& rollNumber, const String& newName, const String& newMacAddress) {
            Student* existing = studentDB.searchByRollNumber(rollNumber);
            if (!existing) {
                if (enable_debug) Serial.println("[ADMIN] Student not found for update.");
                return false;
            }

            // Remove old record and save updated record
            if (!studentDB.deleteStudent(rollNumber)) return false;
            return studentDB.addStudent(newName, rollNumber, newMacAddress);
        }

        // 9. Update class data (matched by classCode)
        bool updateClass(const String& classCode, const String& newClassName,
                         const String& newStartTime, const String& newEndTime, bool newIsActive) {
            ClassRecord* existing = classDB.searchByCode(classCode);
            if (!existing) {
                if (enable_debug) Serial.println("[ADMIN] Class not found for update.");
                return false;
            }

            if (!classDB.deleteClass(classCode)) return false;
            bool added = classDB.addClass(newClassName, newStartTime, newEndTime, classCode, newIsActive);

            if (added && hasActiveClass && activeClass.classCode.equalsIgnoreCase(classCode)) {
                activeClass.className = newClassName;
                activeClass.startTime = newStartTime;
                activeClass.endTime   = newEndTime;
                activeClass.isActive  = newIsActive;
            }
            return added;
        }

        // 10. Link student and class data (stub for now)
        void linkStudentAndClass() {
            // TODO: Implement student-to-class enrollment/linking
        }

        // 11. Export attendance data (stub for now)
        void exportData() {
            // TODO: Export attendance records to CSV/JSON
        }
};