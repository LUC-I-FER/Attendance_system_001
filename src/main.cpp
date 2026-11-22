#include <Arduino.h>
#include "class_db/class_db.h"

ClassDB classDB;

void printClass(const char* label, ClassRecord* c) {
    if (c) {
        Serial.printf("%s -> Found: %s (%s) | %s - %s | Active: %s\n",
                      label,
                      c->className.c_str(),
                      c->classCode.c_str(),
                      c->startTime.c_str(),
                      c->endTime.c_str(),
                      c->isActive ? "YES" : "NO");
    } else {
        Serial.printf("%s -> Not found\n", label);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // 1. Load classes from LittleFS into memory
    if (!classDB.loadClasses()) {
        Serial.println("Failed to load ClassDB!");
        return;
    }

    // 2. Add dummy classes
    classDB.addClass("Data Structures", "09:00", "10:00", "CS201");
    classDB.addClass("Digital Logic",   "10:15", "11:15", "EC102");

    // 3. Search by Class Code
    printClass("Search Code", classDB.searchByCode("cs201"));

    // 4. Search by Class Name
    printClass("Search Name", classDB.searchByName("digital logic"));

    // 5. Activate a class
    if (classDB.setClassActive("CS201", true)) {
        Serial.println("Activated CS201.");
    }

    // 6. Get the currently active class
    printClass("Active Class", classDB.getActiveClass());

    // 7. Delete a class by Class Code
    if (classDB.deleteClass("EC102")) {
        Serial.println("Deleted EC102 successfully.");
    } else {
        Serial.println("Failed to delete EC102.");
    }

    // Verify deletion
    printClass("Verify Delete", classDB.searchByCode("EC102"));
}

void loop() {
    // Nothing needed here for the test
}