#include "fr_forward.h"

#undef min
#undef max
#include "Arduino.h"

#include "FS.h"
#include "SD_MMC.h"
#include "SPI.h"
#include <ArduinoJson.h>
#include <time.h>

namespace Bridge {
    
void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels) {
                listDir(fs, file.path(), levels - 1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

// Face list management functions
void write_face_id_name_list_sdcard(face_id_name_list *l, const char* path) {
    if (SD_MMC.cardType() == CARD_NONE) {
        Serial.println("Write Error: SD Card removed!");
        return;
    }

    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    // Write count first
    file.write(l->count);
    
    // Only traverse if we have faces AND head is valid
    if (l->count > 0 && l->head != NULL) {
        file.write(l->confirm_times);
        
        face_id_node *p = l->head;
        uint8_t saved = 0;
        const int vector_size = FACE_ID_SIZE * sizeof(float);
        
        while (p != NULL && saved < l->count) {
            // Write name
            file.write((uint8_t*)p->id_name, ENROLL_NAME_LEN);
            
            // Write face vector
            if (p->id_vec != NULL && p->id_vec->item != NULL) {
                file.write((uint8_t*)p->id_vec->item, vector_size);
            }
            
            saved++;
            p = p->next;
        }
        
        Serial.printf("Successfully saved %d faces to SD card.\n", saved);
    } else {
        Serial.println("Successfully saved 0 faces to SD card.");
    }
    
    file.close();
}

void read_face_id_name_list_sdcard(face_id_name_list *l, const char *path) {
    if (SD_MMC.cardType() == CARD_NONE) {
        Serial.println("Read Error: SD Card removed!");
        return;
    }

    if (!SD_MMC.exists(path)) {
        Serial.println("No saved face data found.");
        return;
    }

    File file = SD_MMC.open(path, FILE_READ);
    if (!file) return;

    // Read Metadata
    uint8_t count = file.read();
    if (count == 0) {
        file.close();
        Serial.println("No faces in storage.");
        return;
    }
    
    l->confirm_times = file.read();
    l->count = 0;
    l->head = NULL;
    l->tail = NULL;

    const int vector_size = FACE_ID_SIZE * sizeof(float);

    // Reconstruct the Linked List
    for (uint8_t i = 0; i < count; i++) {
        // Allocate memory for the new node (use dl_lib_calloc for consistency)
        face_id_node *new_node = (face_id_node *)dl_lib_calloc(1, sizeof(face_id_node), 0);
        if (new_node == NULL) {
            Serial.println("Memory allocation failed!");
            break;
        }

        // Read the name
        file.read((uint8_t*)new_node->id_name, ENROLL_NAME_LEN);

        // Allocate and read the vector
        new_node->id_vec = dl_matrix3d_alloc(1, 1, 1, FACE_ID_SIZE);
        if (new_node->id_vec == NULL) {
            dl_lib_free(new_node);
            Serial.println("Vector allocation failed!");
            break;
        }
        
        file.read((uint8_t*)new_node->id_vec->item, vector_size);
        new_node->next = NULL;

        // Link the node into the list
        if (l->head == NULL) {
            l->head = new_node;
            l->tail = new_node;
        } else {
            l->tail->next = new_node;
            l->tail = new_node;
        }
        l->count++;
    }

    file.close();
    Serial.printf("Successfully loaded %d faces with names.\n", l->count);
}

// Database and attendance functions
void initSD() {
    if(!SD_MMC.begin()){
        Serial.println("SD Card Mount Failed");
        return;
    }
    
    // Create necessary directories
    if(!SD_MMC.exists("/db")) {
        SD_MMC.mkdir("/db");
        Serial.println("Created /db directory");
    }

    if(!SD_MMC.exists("/atd")) {
        SD_MMC.mkdir("/atd");
        Serial.println("Created /atd directory");
    }
    
    // Create users DB if not exists
    if(!SD_MMC.exists("/db/users.txt")) {
        File file = SD_MMC.open("/db/users.txt", FILE_WRITE);
        file.print("[]"); // Empty JSON array
        file.close();
        Serial.println("Created users.txt");
    }
}

String getUsersJSON() {
    File file = SD_MMC.open("/db/users.txt");
    if(!file) return "[]";
    String data = file.readString();
    file.close();
    return data;
}

bool saveUserToDB(String id, String name, String dept) {
    // Load current users
    File file = SD_MMC.open("/db/users.txt", FILE_READ);
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) { 
        Serial.println("Failed to parse DB"); 
        return false; 
    }

    // Check if user already exists
    JsonArray users = doc.as<JsonArray>();
    for (JsonObject user : users) {
        if (user["name"] == name) {
            Serial.println("User already exists!");
            return false;
        }
    }

    // Add new user
    JsonObject newUser = doc.createNestedObject();
    newUser["id"] = id;
    newUser["name"] = name;
    newUser["dept"] = dept;
    newUser["enrolled"] = true;

    // Save back
    file = SD_MMC.open("/db/users.txt", FILE_WRITE);
    if(serializeJson(doc, file) == 0) {
        file.close();
        return false;
    }
    file.close();
    Serial.printf("User %s saved to database\n", name.c_str());
    return true;
}

// Helper function to get current date string
String getCurrentDate() {
    // For now, use millis-based date. You can add RTC/NTP later
    unsigned long days = millis() / (24 * 60 * 60 * 1000UL);
    return String(days);
}

// Helper function to get current time string
String getCurrentTime() {
    unsigned long totalSeconds = millis() / 1000;
    unsigned long hours = (totalSeconds / 3600) % 24;
    unsigned long minutes = (totalSeconds / 60) % 60;
    unsigned long seconds = totalSeconds % 60;
    
    char timeStr[9];
    sprintf(timeStr, "%02lu:%02lu:%02lu", hours, minutes, seconds);
    return String(timeStr);
}

void logAttendance(String id, String name) {
    // Create filename based on current "date"
    String filename = "/atd/log_" + getCurrentDate() + ".csv";
    
    // Check if user already logged today to prevent duplicates
    if (SD_MMC.exists(filename.c_str())) {
        File checkFile = SD_MMC.open(filename.c_str(), FILE_READ);
        if(checkFile) {
            while(checkFile.available()) {
                String line = checkFile.readStringUntil('\n');
                if(line.indexOf(name) >= 0) {
                    checkFile.close();
                    Serial.printf("⚠ %s already logged today\n", name.c_str());
                    return; // Already logged
                }
            }
            checkFile.close();
        }
    }

    // Open file for appending
    File file = SD_MMC.open(filename.c_str(), FILE_APPEND);
    if(!file) {
        // If file doesn't exist, create it with header
        file = SD_MMC.open(filename.c_str(), FILE_WRITE);
        if (!file) {
            Serial.println("Failed to create attendance file");
            return;
        }
        file.println("ID,Name,Time"); // CSV header
    }
    
    // Log the attendance with timestamp
    String timeStr = getCurrentTime();
    file.printf("%s,%s,%s\n", id.c_str(), name.c_str(), timeStr.c_str());
    file.close();
    
    Serial.printf("✓ Attendance logged: %s at %s\n", name.c_str(), timeStr.c_str());
}

String getAttendanceLogs() {
    // Return attendance logs as JSON for web display
    String filename = "/atd/log_" + getCurrentDate() + ".csv";
    // Serial.println("called get attendance function");
    //   String filename = "/atd/log_0.csv";  
    if (!SD_MMC.exists(filename.c_str())) {
        Serial.println("file not seen on sd");
        return "[]"; // No logs today
        
    }
    
    File file = SD_MMC.open(filename.c_str(), FILE_READ);
    if (!file) {
                Serial.println("file seen but failed to open");
        return "[]";}
    
    DynamicJsonDocument doc(4096);
    JsonArray logs = doc.to<JsonArray>();
    
    // Skip header line
    if (file.available()) {
        file.readStringUntil('\n');
    }
    
    // Parse CSV lines
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) continue;
        
        // Parse CSV: ID,Name,Time
        int firstComma = line.indexOf(',');
        int secondComma = line.indexOf(',', firstComma + 1);
        
        if (firstComma > 0 && secondComma > firstComma) {
            JsonObject entry = logs.createNestedObject();
            entry["id"] = line.substring(0, firstComma);
            entry["name"] = line.substring(firstComma + 1, secondComma);
            entry["time"] = line.substring(secondComma + 1);
        }
    }
    
    file.close();
    
    String output;

    serializeJson(doc, output);
    return output;
}

String downloadAttendanceCSV() {
    // Return raw CSV content for download
    String filename = "/atd/log_" + getCurrentDate() + ".csv";
    
    if (!SD_MMC.exists(filename.c_str())) {
        return "ID,Name,Time\n"; // Empty CSV with header
    }
    
    File file = SD_MMC.open(filename.c_str(), FILE_READ);
    if (!file) return "ID,Name,Time\n";
    
    String content = file.readString();
    file.close();
    
    return content;
}

bool clearAttendanceLogs() {
    // Delete today's attendance log
    String filename = "/atd/log_" + getCurrentDate() + ".csv";
    
    if (SD_MMC.exists(filename.c_str())) {
        if (SD_MMC.remove(filename.c_str())) {
            Serial.println("Attendance logs cleared");
            return true;
        } else {
            Serial.println("Failed to delete logs");
            return false;
        }
    }
    
    Serial.println("No logs to clear");
    return true;
}

bool deleteUserFromDB(String name) {
    File file = SD_MMC.open("/db/users.txt", FILE_READ);
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.println("Failed to parse DB for deletion");
        return false;
    }

    JsonArray users = doc.as<JsonArray>();
    bool found = false;
    
    for (int i = 0; i < users.size(); i++) {
        if (users[i]["name"] == name) {
            users.remove(i);
            found = true;
            break;
        }
    }

    if (!found) {
        Serial.printf("User %s not found in database\n", name.c_str());
        return false;
    }

    file = SD_MMC.open("/db/users.txt", FILE_WRITE);
    if (serializeJson(doc, file) == 0) {
        file.close();
        return false;
    }
    file.close();
    
    Serial.printf("User %s deleted from database\n", name.c_str());
    return true;
}

} // namespace Bridge
