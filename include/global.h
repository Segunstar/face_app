#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include "fd_forward.h"
#include "fr_forward.h"

// ─── Attendance mode flag ────────────────────────────────────────────────────
extern bool isAttendanceMode;          // true = running face recognition loop
extern unsigned long lastRecognitionTime;
extern const unsigned long RECOGNITION_COOLDOWN;

// ─── Face recognition objects (defined in app_httpd.cpp) ────────────────────
extern mtmn_config_t mtmn_config;
extern face_id_name_list id_list;

// ─── Web-server control flags ────────────────────────────────────────────────
extern int8_t detection_enabled;
extern int8_t recognition_enabled;
extern int8_t is_enrolling;
extern bool   authenticated;

// ─── Enroll context (set when admin opens enroll modal) ─────────────────────
extern String newUserId;
extern String newUserName;
extern String newUserDept;

// ─── Hardware pins ────────────────────────────────────────────────────────────
#define LED_GPIO_NUM    4
#define FLASH_GPIO_NUM  4
#define BUZZER_GPIO_NUM 13   // Optional buzzer – connect between GPIO13 & GND

// ─── Settings structure (persisted to /db/settings.json) ─────────────────────
struct AttendanceSettings {
    char  startTime[6];     // "07:30"
    char  endTime[6];       // "18:00"
    char  lateTime[6];      // "08:10"
    char  absentTime[6];    // "10:00"
    int   confidence;       // 85  (recognition confidence %, not used by MTMN directly but stored for UI)
    bool  buzzerEnabled;    // flash/buzzer feedback on recognition
    bool  autoMode;         // attendance detection always-on when not in admin mode
    long  gmtOffsetSec;     // seconds east of UTC, e.g. 3600 for UTC+1 (Nigeria = 3600)
    char  ntpServer[64];    // "pool.ntp.org"
    char  ssid[32];         // stored so dashboard can display it
};

extern AttendanceSettings gSettings;
extern bool ntpSynced;

// ─── Helper: default settings ────────────────────────────────────────────────
inline void setDefaultSettings(AttendanceSettings &s) {
    strncpy(s.startTime,   "07:30",       sizeof(s.startTime));
    strncpy(s.endTime,     "18:00",       sizeof(s.endTime));
    strncpy(s.lateTime,    "08:10",       sizeof(s.lateTime));
    strncpy(s.absentTime,  "10:00",       sizeof(s.absentTime));
    s.confidence    = 85;
    s.buzzerEnabled = false;
    s.autoMode      = true;
    s.gmtOffsetSec  = 3600;   // UTC+1 (Nigeria / WAT)
    strncpy(s.ntpServer, "pool.ntp.org", sizeof(s.ntpServer));
    strncpy(s.ssid,      "unknown",      sizeof(s.ssid));
}

#endif // GLOBALS_H
