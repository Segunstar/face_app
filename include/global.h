#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include "fd_forward.h"
#include "fr_forward.h"

// ─── Attendance mode flag ────────────────────────────────────────────────────
extern bool isAttendanceMode;
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

// ─── Hardware pins ────────────────────────────────────────────────────────────
// AI-Thinker ESP32-CAM free output GPIOs (camera + SPI SD accounted for):
//   GPIO  2 – SD_MISO  (DO NOT USE as output)
//   GPIO  4 – Onboard flash LED
//   GPIO 12 – Free ✓  → Green status LED
//   GPIO 13 – Free ✓  → Red LED + active buzzer (wired in parallel on same pin)
//   GPIO 14 – SD_SCK   (DO NOT USE as output)
//   GPIO 15 – SD_MOSI  (DO NOT USE as output)
//   GPIO 33 – SD_CS    (DO NOT USE as output)
//
// ── Wiring for GPIO 13 (red LED + buzzer sharing one pin) ─────────────────────
// Drive via an NPN transistor (e.g. 2N2222) to handle combined current draw:
//
//   GPIO 13 ──1kΩ── NPN base
//   NPN emitter ── GND
//   3.3V ──┬──220Ω── Red LED anode → cathode ──┐
//          └──────── Buzzer (+)                 ├── NPN collector
//                    (both share the collector) ┘
//
// ── Wiring for GPIO 12 (green LED only) ───────────────────────────────────────
//   GPIO 12 ──220Ω── Green LED anode → cathode ── GND
//   Add 10kΩ pull-down to GND (GPIO 12 is a strapping pin – must be LOW at boot)
//
#define FLASH_GPIO_NUM   4    // onboard white flash LED (camera use only)
#define GREEN_LED_GPIO  12    // green status LED (recognised / access granted)
#define RED_LED_GPIO    13    // red LED  ┐ wired in parallel on same GPIO
#define BUZZER_GPIO_NUM 13    // buzzer   ┘ (both driven by NPN on GPIO 13)

// ─── Settings structure (persisted to /cfg/settings.json) ─────────────────────
struct AttendanceSettings {
    char  startTime[6];
    char  endTime[6];
    char  lateTime[6];
    char  absentTime[6];
    int   confidence;
    bool  buzzerEnabled;
    bool  autoMode;
    long  gmtOffsetSec;
    char  ntpServer[64];
    char  ssid[32];
};

extern AttendanceSettings gSettings;
extern bool ntpSynced;

// ─── User record ──────────────────────────────────────────────────────────────
struct UserRecord {
    char id[32];
    char name[64];
    char dept[64];
    char role[32];

    static UserRecord make(const char *_id, const char *_name,
                           const char *_dept, const char *_role = "Student") {
        UserRecord r = {};
        strncpy(r.id,   _id,   sizeof(r.id)   - 1);
        strncpy(r.name, _name, sizeof(r.name) - 1);
        strncpy(r.dept, _dept, sizeof(r.dept) - 1);
        strncpy(r.role, _role, sizeof(r.role) - 1);
        return r;
    }
};

// ─── Attendance record ────────────────────────────────────────────────────────
struct AttendanceRecord {
    char uid[32];
    char name[64];
    char dept[64];
    char date[12];
    char time[6];
    char status[16];
    char notes[64];
    char confidence[8];

    static AttendanceRecord fromFace(const char *_uid, const char *_name,
                                     const char *_dept = "") {
        AttendanceRecord r = {};
        strncpy(r.uid,  _uid,  sizeof(r.uid)  - 1);
        strncpy(r.name, _name, sizeof(r.name) - 1);
        strncpy(r.dept, _dept, sizeof(r.dept) - 1);
        strncpy(r.confidence, "92%", sizeof(r.confidence) - 1);
        return r;
    }
};

// ─── Enrolment context ────────────────────────────────────────────────────────
struct EnrollContext {
    char id[32];
    char name[64];
    char dept[64];
};
extern EnrollContext enrollCtx;

// ─── Helper: default settings ────────────────────────────────────────────────
inline void setDefaultSettings(AttendanceSettings &s) {
    strncpy(s.startTime,   "07:30",       sizeof(s.startTime));
    strncpy(s.endTime,     "18:00",       sizeof(s.endTime));
    strncpy(s.lateTime,    "08:10",       sizeof(s.lateTime));
    strncpy(s.absentTime,  "10:00",       sizeof(s.absentTime));
    s.confidence    = 85;
    s.buzzerEnabled = false;
    s.autoMode      = true;
    s.gmtOffsetSec  = 3600;
    strncpy(s.ntpServer, "pool.ntp.org", sizeof(s.ntpServer));
    strncpy(s.ssid,      "unknown",      sizeof(s.ssid));
}

#endif // GLOBALS_H