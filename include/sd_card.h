#ifndef WRAPPER_H
#define WRAPPER_H

#include "fr_forward.h"
#include "FS.h"
#include "SD_MMC.h"
#include <ArduinoJson.h>

namespace Bridge {
    void listDir(fs::FS &fs, const char *dirname, uint8_t levels);
    
    // Face ID management
    void read_face_id_name_list_sdcard(face_id_name_list *l, const char *path);
    void write_face_id_name_list_sdcard(face_id_name_list *l, const char *path);

    // User database management
    bool saveUserToDB(String id, String name, String dept);
    bool deleteUserFromDB(String name);
    String getUsersJSON(); // Returns JSON string for the web frontend
    
    // Attendance logging
    void logAttendance(String id, String name); // Saves to /atd/log_DATE.csv
    String getAttendanceLogs(); // Returns JSON array of today's attendance
    String downloadAttendanceCSV(); // Returns raw CSV content for download
    bool clearAttendanceLogs(); // Deletes today's attendance log
    
    // SD card initialization
    void initSD();
}

#endif
