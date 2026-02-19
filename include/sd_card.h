#ifndef WRAPPER_H
#define WRAPPER_H

// ─────────────────────────────────────────────────────────────────────────────
//  FaceGuard Pro  –  sd_card.h
//  SD / persistence layer built on greiman/SdFat (v2) instead of Arduino
//  SD_MMC, giving full long-filename (LFN) support on FAT32 / exFAT cards.
// ─────────────────────────────────────────────────────────────────────────────

#include "fr_forward.h"
#include <SdFat.h>
#include <ArduinoJson.h>
#include "global.h"

namespace Bridge {

    // ── SD initialisation (creates /db, /atd, /cfg, /db/users.txt if missing)
    void initSD();

    // ── SD state query ────────────────────────────────────────────────────────
    // Returns true if the SD card mounted successfully (or was remounted after
    // a transient failure).  Check this before relying on any SD operation.
    bool sdIsOk();

    // ── Directory helper ─────────────────────────────────────────────────────
    void listDir(const char *dirname, uint8_t levels);

    // ── Face-ID binary persistence ──────────────────────────────────────────
    void read_face_id_name_list_sdcard(face_id_name_list *l, const char *path);
    void write_face_id_name_list_sdcard(face_id_name_list *l, const char *path);

    // ── User database (JSON array in /db/users.txt) ──────────────────────────
    bool   saveUserToDB(const UserRecord &user);
    bool   deleteUserFromDB(const char *name);
    String getUsersJSON();
    int    getUserCount();

    // Look up a user by their display name (as stored in FACE.BIN id_name).
    // Fills 'out' with id / name / dept / role from users.txt.
    // Returns true if found, false if not in DB.
    // Use this after face recognition to get the correct UID and department
    // instead of passing match->id_name as both uid and name.
    bool getUserByName(const char *name, UserRecord &out);

    // ── Attendance logging ────────────────────────────────────────────────────
    // logAttendance – auto path: date / time / status filled from current time.
    void logAttendance(const AttendanceRecord &rec);

    // manualAttendance – admin override; date/time/status taken from rec fields.
    bool manualAttendance(const AttendanceRecord &rec);

    // ── Attendance queries ────────────────────────────────────────────────────
    String getLogsJSON(String date, String dept, String status, String search);
    String getLogsRange(int days);
    String getAttendanceLogs();
    String downloadAttendanceCSV(String date = "");
    bool   clearAttendanceLogs(String date = "");

    // ── Factory reset ─────────────────────────────────────────────────────────
    // Wipes all SD data to a clean slate: deletes every attendance log,
    // FACE.BIN, users.txt (reset to []), and settings.json, then recreates
    // the directory structure.  The caller must also clear the in-memory
    // face id_list (done in api_factory_reset_handler in app_httpd.cpp).
    bool   factoryReset();

    // ── Dashboard / storage / status ─────────────────────────────────────────
    String getStatsJSON();
    String getStorageJSON();
    String getStatusJSON();

    // ── Settings ─────────────────────────────────────────────────────────────
    bool loadSettings(AttendanceSettings &s);
    bool saveSettings(const AttendanceSettings &s);

    // ── NTP ──────────────────────────────────────────────────────────────────
    void   syncNTP();
    String getCurrentDateStr();
    String getCurrentTimeStr();
    String getCurrentHHMM();

} // namespace Bridge

#endif // WRAPPER_H
