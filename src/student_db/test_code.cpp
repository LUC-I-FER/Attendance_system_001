#include <Arduino.h>
#include "student_db/student_db.h"

StudentDB db;

void printStudent(const char* label, Student* s) {
    if (s) {
        Serial.printf("%s -> Found: %s | Roll: %s | MAC: %s\n",
                      label, s->name.c_str(), s->rollNumber.c_str(), s->macAddress.c_str());
    } else {
        Serial.printf("%s -> Not found\n", label);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // 1. Load students from LittleFS into memory
    if (!db.loadStudents()) {
        Serial.println("Failed to load StudentDB!");
        return;
    }

    // 2. Add students
    db.addStudent("Aarav Sharma", "CS101", "AA:BB:CC:11:22:33");
    db.addStudent("Priya Patel",  "CS102", "AA:BB:CC:44:55:66");

    // 3. Search by Name
    printStudent("Search Name", db.searchByName("aarav sharma"));

    // 4. Search by Roll Number
    printStudent("Search Roll", db.searchByRollNumber("CS102"));

    // 5. Search by MAC Address
    printStudent("Search MAC ", db.searchByMacAddress("aa:bb:cc:11:22:33"));

    // 6. Delete a student by Roll Number
    if (db.deleteStudent("CS101")) {
        Serial.println("Deleted CS101 successfully.");
    } else {
        Serial.println("Failed to delete CS101.");
    }

    // Verify deletion
    printStudent("Verify Roll", db.searchByRollNumber("CS101"));
}

void loop() {
    // Nothing needed here for the test
}